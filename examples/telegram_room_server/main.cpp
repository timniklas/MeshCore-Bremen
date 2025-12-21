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
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Telegram send: WiFi not connected");
    return;
  }

  HTTPClient http;
  String url = String("https://api.telegram.org/bot") + TELEGRAM_BOT_TOKEN + "/sendMessage";
  http.begin(url);
  http.addHeader("Content-Type", "application/json");

  // build JSON payload; send chat_id as string (works for @username or numeric ids)
  String payload = "{\"chat_id\":\"";
  payload += String(TELEGRAM_CHAT_ID);
  payload += "\",\"text\":\"";
  // rudimentary escaping for quotes and backslashes
  String t = String(text);
  t.replace("\\", "\\\\");
  t.replace("\"", "\\\"");
  payload += t;
  payload += "\"}";

  int code = http.POST(payload);
  String resp = http.getString(); // read response body (may be empty)

  Serial.print("Telegram send status: ");
  Serial.println(code);
  if (resp.length()) {
    Serial.print("Telegram response: ");
    Serial.println(resp);
  }

  if (code != 200) {
    // Helpful hint in log for common 403 causes
    if (code == 403) {
      Serial.println("Telegram 403: check bot token, chat_id, bot membership/permissions or if bot is blocked.");
    }
  }

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

    // Helper: find matching '}' for '{' at pos, returns index of matching '}', or -1
    auto findMatchingBrace = [&](int start) -> int {
      int depth = 0;
      int L = payload.length();
      for (int i = start; i < L; i++) {
        char c = payload.charAt(i);
        if (c == '{') depth++;
        else if (c == '}') {
          depth--;
          if (depth == 0) return i;
        }
      }
      return -1;
    };

    // Helper: extract top-level string field inside [blkStart..blkEnd] for field name (depth==1)
    auto extractTopLevelString = [&](int blkStart, int blkEnd, const char* field) -> String {
      int L = payload.length();
      int fldLen = strlen(field);
      int depth = 0;
      for (int i = blkStart; i <= blkEnd; i++) {
        char c = payload.charAt(i);
        if (c == '{') { depth++; continue; }
        if (c == '}') { depth--; continue; }
        if (depth == 1 && payload.charAt(i) == '"') {
          int j = i + 1;
          // read field name
          int k = j;
          while (k <= blkEnd && payload.charAt(k) != '"' ) k++;
          if (k > blkEnd) break;
          if (k - j == fldLen && strncmp(field, payload.substring(j, k).c_str(), fldLen) == 0) {
            // found "field"
            // find ':' after k
            int colon = payload.indexOf(':', k);
            if (colon < 0 || colon > blkEnd) return String();
            // skip spaces
            int vstart = colon + 1;
            while (vstart <= blkEnd && isspace(payload.charAt(vstart))) vstart++;
            if (vstart > blkEnd) return String();
            if (payload.charAt(vstart) == '"') {
              int q1 = vstart;
              int q2 = q1 + 1;
              while (q2 <= blkEnd) {
                char ch = payload.charAt(q2);
                if (ch == '"' ) {
                  // check not escaped
                  int back = q2 - 1;
                  int esc = 0;
                  while (back > q1 && payload.charAt(back) == '\\') { esc++; back--; }
                  if ((esc & 1) == 0) break;
                }
                q2++;
              }
              if (q2 > blkEnd) return String();
              String val = payload.substring(q1 + 1, q2);
              // minimal unescape
              val.replace("\\\"", "\"");
              val.replace("\\\\", "\\");
              return val;
            } else {
              // not a string (number/null) -> return empty
              return String();
            }
          }
          i = k;
        }
      }
      return String();
    };

    // Helper: detect if a top-level field exists (depth==1), e.g. "photo"
    auto existsTopLevelField = [&](int blkStart, int blkEnd, const char* field) -> bool {
      int L = payload.length();
      int fldLen = strlen(field);
      int depth = 0;
      for (int i = blkStart; i <= blkEnd; i++) {
        char c = payload.charAt(i);
        if (c == '{') { depth++; continue; }
        if (c == '}') { depth--; continue; }
        if (depth == 1 && payload.charAt(i) == '"') {
          int j = i + 1;
          int k = j;
          while (k <= blkEnd && payload.charAt(k) != '"') k++;
          if (k > blkEnd) break;
          if (k - j == fldLen && strncmp(field, payload.substring(j, k).c_str(), fldLen) == 0) return true;
          i = k;
        }
      }
      return false;
    };

    // Helper: extract numeric id inside a sub-object named "from" at depth==1
    auto extractFromId = [&](int blkStart, int blkEnd) -> long {
      int depth = 0;
      int L = payload.length();
      for (int i = blkStart; i <= blkEnd; i++) {
        char c = payload.charAt(i);
        if (c == '{') { depth++; continue; }
        if (c == '}') { depth--; continue; }
        if (depth == 1 && payload.charAt(i) == '"') {
          int j = i + 1;
          int k = j;
          while (k <= blkEnd && payload.charAt(k) != '"') k++;
          if (k > blkEnd) break;
          if (strncmp("from", payload.substring(j, k).c_str(), 4) == 0) {
            // find '{' for from block
            int brace = payload.indexOf('{', k);
            if (brace < 0 || brace > blkEnd) return 0;
            int endb = findMatchingBrace(brace);
            if (endb < 0) return 0;
            // inside from block search "id": number
            int pos_id = payload.indexOf("\"id\"", brace);
            if (pos_id < 0 || pos_id > endb) return 0;
            int colon = payload.indexOf(':', pos_id);
            if (colon < 0 || colon > endb) return 0;
            int numStart = colon + 1;
            while (numStart <= endb && isspace(payload.charAt(numStart))) numStart++;
            int numEnd = numStart;
            while (numEnd <= endb && (payload.charAt(numEnd) == '-' || isdigit(payload.charAt(numEnd)))) numEnd++;
            if (numEnd > numStart) {
              String num = payload.substring(numStart, numEnd);
              num.trim();
              return num.toInt();
            }
            return 0;
          }
        }
      }
      return 0;
    };

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

      int pos_msg = payload.indexOf("\"message\"", pos_update);
      if (pos_msg < 0) { idx = comma + 1; continue; }
      int brace = payload.indexOf('{', pos_msg);
      if (brace < 0) { idx = comma + 1; continue; }
      int msgEnd = findMatchingBrace(brace);
      if (msgEnd < 0) { idx = comma + 1; continue; }

      // If message contains a photo, inject an emoji. Prefer caption if present.
      bool hasPhoto = existsTopLevelField(brace, msgEnd, "photo");
      String caption = extractTopLevelString(brace, msgEnd, "caption");
      String text;

      if (hasPhoto) {
        // Camera emoji U+1F4F7 (UTF-8: F0 9F 93 B7)
        const char cameraEmoji[] = "\xF0\x9F\x93\xB7";
        if (caption.length() > 0) {
          text = String(cameraEmoji) + " " + caption;
        } else {
          text = String(cameraEmoji);
        }
      } else {
        // extract top-level text inside the message block only
        text = extractTopLevelString(brace, msgEnd, "text");
        if (text.length() == 0) {
          // no text field at top-level -> skip
          idx = comma + 1;
          continue;
        }
      }

      if (update_id > telegram_last_update_id) telegram_last_update_id = update_id;

      // extract from.id and username if present
      long tg_user_id = extractFromId(brace, msgEnd);
      String tg_username = extractTopLevelString(brace, msgEnd, "username"); // often top-level absent; safe to try

      // build tmpClient as before
      ClientInfo tmpClient;
      memset(&tmpClient, 0, sizeof(tmpClient));
      uint32_t uid32 = (uint32_t)tg_user_id;
      tmpClient.id = the_mesh.getSelfId(); // initialize structure
      for (int b = 0; b < PUB_KEY_SIZE; b++) {
        uint8_t v = 0;
        if (b < 4) v = (uid32 >> (8 * (3 - b))) & 0xFF;
        else if (tg_username.length() > 0) v = (uint8_t)tg_username[(b - 4) % tg_username.length()];
        tmpClient.id.pub_key[b] = v;
      }
      tmpClient.last_timestamp = 0;

      g_isInjectingTelegram = true;
      the_mesh.addPost(&tmpClient, text.c_str());
      g_isInjectingTelegram = false;

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
  // Initialize telegramClient to use the server identity so injected Telegram messages appear with known sender
  memset(&telegramClient, 0, sizeof(telegramClient));
  telegramClient.id = the_mesh.getSelfId();   // set author identity to this node (known identity)
  telegramClient.last_timestamp = 0;
#endif

  // ---- Load network/telegram config from filesystem (if present) ----
  // default values come from build-time macros (string literals)
  String cfg_ssid = String(WIFI_SSID);
  String cfg_pass = String(WIFI_PASS);
  String cfg_bot_token = String(TELEGRAM_BOT_TOKEN);
  String cfg_chat_id = String(TELEGRAM_CHAT_ID);

  const char *nc_name = "/net_config";
  if (SPIFFS.exists(nc_name)) {
    File nc = SPIFFS.open(nc_name);
    if (nc) {
      while (nc.available()) {
        String line = nc.readStringUntil('\n');
        line.trim();
        if (line.length() == 0) continue;
        int eq = line.indexOf('=');
        if (eq <= 0) continue;
        String k = line.substring(0, eq);
        String v = line.substring(eq + 1);
        k.trim(); v.trim();
        if (k == "WIFI_SSID") cfg_ssid = v;
        else if (k == "WIFI_PASS") cfg_pass = v;
        else if (k == "TELEGRAM_BOT_TOKEN") cfg_bot_token = v;
        else if (k == "TELEGRAM_CHAT_ID") cfg_chat_id = v;
      }
      nc.close();
    }
  } else {
    // create default net_config with build-time values so defaults are persistent
    File nw = SPIFFS.open(nc_name, "w");
    if (nw) {
      nw.printf("WIFI_SSID=%s\n", cfg_ssid.c_str());
      nw.printf("WIFI_PASS=%s\n", cfg_pass.c_str());
      nw.printf("TELEGRAM_BOT_TOKEN=%s\n", cfg_bot_token.c_str());
      nw.printf("TELEGRAM_CHAT_ID=%s\n", cfg_chat_id.c_str());
      nw.close();
      Serial.println("Created default /net_config");
    } else {
      Serial.println("Failed to create /net_config");
    }
  }
  // optional: print loaded values (avoid printing token in cleartext unless needed)
  Serial.print("Configured WiFi SSID: ");
  Serial.println(cfg_ssid);
  // Serial.print("Configured Telegram token: "); Serial.println(cfg_bot_token);

  // WLAN verbinden (use loaded config)
  WiFi.begin(cfg_ssid.c_str(), cfg_pass.c_str());
  Serial.print("Connecting to WiFi ");
  Serial.println(cfg_ssid);
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

#ifdef DISPLAY_CLASS
  ui_task.begin(the_mesh.getNodePrefs(), FIRMWARE_BUILD_DATE, FIRMWARE_VERSION);
#endif

  // send out initial Advertisement to the mesh (only when online to avoid split-brain)
#ifdef ESP32
  // Always send an initial advertisement after boot (helps discovery)
  the_mesh.sendSelfAdvertisement(16000);
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
