#include <Arduino.h>   // needed for PlatformIO
#include <Mesh.h>

#ifdef ESP32
#include <WiFi.h>
#include <HTTPClient.h>
#endif

#include "MyMesh.h"

#ifdef DISPLAY_CLASS
  #include "UITask.h"
  static UITask ui_task(display);
#endif

StdRNG fast_rng;
SimpleMeshTables tables;
MyMesh the_mesh(board, radio_driver, *new ArduinoMillis(), fast_rng, rtc_clock, tables);

void halt() {
  while (1) ;
}

static char command[MAX_POST_TEXT_LEN+1];

// Insert: start ESP32-specific block
#ifdef ESP32

// --- Konfiguration: bitte anpassen ---
#ifndef WIFI_SSID
#define WIFI_SSID "your_ssid"
#endif
#ifndef WIFI_PASS
#define WIFI_PASS "your_password"
#endif
#ifndef TELEGRAM_BOT_TOKEN
#define TELEGRAM_BOT_TOKEN "YOUR_BOT_TOKEN"
#endif
#ifndef TELEGRAM_CHAT_ID
#define TELEGRAM_CHAT_ID "YOUR_CHAT_ID"
#endif
#define TELEGRAM_POLL_INTERVAL_MS 5000
// ------------------------------------

// Globals (non-static so other TUs can extern them)
bool g_isInjectingTelegram = false;
bool g_isOnline = false;
int32_t telegram_last_update_id = 0;

ClientInfo telegramClient; // temporärer "Autor" für Telegram-Nachrichten

static String urlEncode(const String &s) {
  String r;
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (('0' <= c && c <= '9') || ('a' <= c && c <= 'z') || ('A' <= c && c <= 'Z') ||
        c == '-' || c == '_' || c == '.' || c == '~') {
      r += c;
    } else if (c == ' ') {
      r += '%'; r += '2'; r += '0';
    } else {
      char buf[4];
      sprintf(buf, "%%%02X", (uint8_t)c);
      r += buf;
    }
  }
  return r;
}

void sendTelegramMessage(const char *text) {
  #ifdef ESP32
  if (WiFi.status() != WL_CONNECTED) return;
  HTTPClient http;
  String t = urlEncode(String(text));
  String u = String("https://api.telegram.org/bot") + TELEGRAM_BOT_TOKEN + "/sendMessage?chat_id=" + TELEGRAM_CHAT_ID +
             "&text=" + t;
  http.begin(u);
  int code = http.GET();
  // optional: check code, read response if needed
  http.end();
  #endif
}

void fetchTelegramUpdates() {
  #ifdef ESP32
  if (WiFi.status() != WL_CONNECTED) { g_isOnline = false; return; }
  HTTPClient http;
  String u = String("https://api.telegram.org/bot") + TELEGRAM_BOT_TOKEN + "/getUpdates?timeout=0";
  if (telegram_last_update_id > 0) {
    u += "&offset=" + String((long)(telegram_last_update_id + 1));
  }
  http.begin(u);
  int code = http.GET();
  if (code == HTTP_CODE_OK) {
    g_isOnline = true; // successful contact with Telegram API
    String payload = http.getString();
    // Einfache, robuste (aber minimalistische) JSON-Extraktion:
    int idx = 0;
    while (true) {
      int pos_update = payload.indexOf("\"update_id\"", idx);
      if (pos_update < 0) break;
      int colon = payload.indexOf(':', pos_update);
      if (colon < 0) break;
      int comma = payload.indexOf(',', colon);
      if (comma < 0) break;
      String sid = payload.substring(colon + 1, comma);
      sid.trim();
      long update_id = sid.toInt();
      // Suche message.text
      int pos_msg = payload.indexOf("\"message\"", pos_update);
      int pos_text = payload.indexOf("\"text\"", pos_msg);
      if (pos_text >= 0) {
        int q1 = payload.indexOf('"', pos_text + 7); // opening quote of value
        int q2 = payload.indexOf('"', q1 + 1);
        if (q1 >= 0 && q2 > q1) {
          String text = payload.substring(q1 + 1, q2);
          // unescape rudimentär (nur für \" und \\)
          text.replace("\\\"", "\"");
          text.replace("\\\\", "\\");
          if (update_id > telegram_last_update_id) telegram_last_update_id = update_id;
          // injiziere als Post in den Mesh-Server
          g_isInjectingTelegram = true;
          the_mesh.addPost(&telegramClient, text.c_str());
          g_isInjectingTelegram = false;
        }
      }
      idx = comma + 1;
    }
  } else {
    // unable to reach Telegram API right now
    g_isOnline = false;
  }
  http.end();
  #endif
}
// Close ESP32-specific block (this matches the existing #endif already present)
#endif // ESP32

void setup() {
  Serial.begin(115200);
  delay(1000);

  board.begin();

#ifdef DISPLAY_CLASS
  if (display.begin()) {
    display.startFrame();
    display.setCursor(0, 0);
    display.print("Please wait...");
    display.endFrame();
  }
#endif

  if (!radio_init()) { halt(); }

  fast_rng.begin(radio_get_rng_seed());

  FILESYSTEM* fs;
#if defined(NRF52_PLATFORM)
  InternalFS.begin();
  fs = &InternalFS;
  IdentityStore store(InternalFS, "");
#elif defined(RP2040_PLATFORM)
  LittleFS.begin();
  fs = &LittleFS;
  IdentityStore store(LittleFS, "/identity");
  store.begin();
#elif defined(ESP32)
  SPIFFS.begin(true);
  fs = &SPIFFS;
  IdentityStore store(SPIFFS, "/identity");
#else
  #error "need to define filesystem"
#endif
  if (!store.load("_main", the_mesh.self_id)) {
    the_mesh.self_id = radio_new_identity();   // create new random identity
    int count = 0;
    while (count < 10 && (the_mesh.self_id.pub_key[0] == 0x00 || the_mesh.self_id.pub_key[0] == 0xFF)) {  // reserved id hashes
      the_mesh.self_id = radio_new_identity(); count++;
    }
    store.save("_main", the_mesh.self_id);
  }

  Serial.print("Room ID: ");
  mesh::Utils::printHex(Serial, the_mesh.self_id.pub_key, PUB_KEY_SIZE); Serial.println();

  command[0] = 0;

  sensors.begin();

  the_mesh.begin(fs);

#ifdef ESP32
  // WLAN verbinden
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("Connecting to WiFi ");
  Serial.println(WIFI_SSID);
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 20000) {
    delay(200);
    Serial.print(".");
  }
  Serial.println();
  g_isOnline = (WiFi.status() == WL_CONNECTED); // <-- set online-flag
  if (g_isOnline) {
    Serial.print("WiFi connected, IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("WiFi connect failed (continuing offline)");
  }
#endif

#ifdef DISPLAY_CLASS
  ui_task.begin(the_mesh.getNodePrefs(), FIRMWARE_BUILD_DATE, FIRMWARE_VERSION);
#endif

  // send out initial Advertisement to the mesh (only when online to avoid split-brain)
#ifdef ESP32
  if (g_isOnline) {
    the_mesh.sendSelfAdvertisement(16000);
  } else {
    Serial.println("Skipping initial advert: offline");
  }
#else
  the_mesh.sendSelfAdvertisement(16000);
#endif
}

void loop() {
  static unsigned long nextTelegramPoll = 0;
  int len = strlen(command);
  while (Serial.available() && len < sizeof(command)-1) {
    char c = Serial.read();
    if (c != '\n') {
      command[len++] = c;
      command[len] = 0;
    }
    Serial.print(c);
  }
  if (len == sizeof(command)-1) {  // command buffer full
    command[sizeof(command)-1] = '\r';
  }

  if (len > 0 && command[len - 1] == '\r') {  // received complete line
    command[len - 1] = 0;  // replace newline with C string null terminator
    char reply[160];
    the_mesh.handleCommand(0, command, reply);  // NOTE: there is no sender_timestamp via serial!
    if (reply[0]) {
      Serial.print("  -> "); Serial.println(reply);
    }

    command[0] = 0;  // reset command buffer
  }

  the_mesh.loop();
  sensors.loop();
#ifdef DISPLAY_CLASS
  ui_task.loop();
#endif
  rtc_clock.tick();

#ifdef ESP32
  if (millis() >= nextTelegramPoll) {
    fetchTelegramUpdates();
    nextTelegramPoll = millis() + TELEGRAM_POLL_INTERVAL_MS;
  }
#endif
}
