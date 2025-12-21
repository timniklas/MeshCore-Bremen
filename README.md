# 🇩🇪 MeshCore – Bremen Custom Firmware

Dies ist ein **regionaler Fork** von [MeshCore](https://github.com/meshcore-dev/MeshCore),  
der zusätzliche, optionale Firmware-Rollen für das **Bremer Mesh-Netzwerk** bereitstellt.  

Die Basisfunktionen und das Protokoll von MeshCore bleiben **unverändert** –  
dieser Fork ergänzt lediglich eigene Spezial-Firmwares (Custom Roles),  
die mit dem offiziellen MeshCore-Ökosystem vollständig kompatibel sind.

---

## 🌐 Einstieg & Dokumentation

➡️ **Lokale Anleitung & Willkommensseite:**  
[https://meshcore.bremesh.de/moin](https://meshcore.bremesh.de/moin)

➡️ **Offizielle MeshCore-Doku:**  
[https://github.com/meshcore-dev/MeshCore](https://github.com/meshcore-dev/MeshCore)

---

## ⚙️ Custom Firmware-Rollen

Dieser Fork ergänzt den offiziellen MeshCore-Flasher um neue Rollen,  
die du über den **„Custom Firmware“-Button** (ganz unten im Web-Flasher) installieren kannst:

👉 **[https://flasher.meshcore.co.uk](https://flasher.meshcore.co.uk)**

Wähle dein Board, klicke auf **„Custom Firmware“**,  
und lade die gewünschte `.bin`-Datei hoch (z. B. `ping_server.bin`).

---

### 🆕 Verfügbare Custom-Rollen

#### 🛰️ Ping Server
**Zweck:** Antwortet automatisch auf `ping`-Nachrichten im Kanal `#ping`.

**Nutzen:**
- Testet Reichweite & Paketweiterleitung im Bremer Mesh  
- Misst Antwortzeiten (RTT) zwischen Knoten  
- Ideal für feste Standorte (z. B. Solar- oder Dachnodes)

**Verhalten:**
- Keine Benutzeroberfläche  
- Kein Chat, keine Weiterleitung anderer Nachrichten  
- Minimaler Energieverbrauch  

**Typische Verwendung:**
```text
Companion → sendet ping → Ping Server antwortet → Reichweite sichtbar
```

#### 🤖 Telegram Room Server

Eine optionale Rolle, die eingehende/ausgehende Nachrichten zwischen dem Mesh und einem Telegram‑Chat bridged.

Kurz:
- Leitet neue Mesh‑Posts an einen konfigurierten Telegram‑Chat weiter.
- Injiziert Telegram‑Nachrichten als Posts in den Room Server.

Wichtiges Verhalten und Features:
- Änderungen per serieller Konsole:
  - set net ssid <WIFI_SSID>
  - set net pass <WIFI_PASS>
  - set net token <TELEGRAM_BOT_TOKEN>
  - set net chat <TELEGRAM_CHAT_ID>  ← ändert Chat und löscht lokalen Chat‑Cache
  - show net  (zeigt aktuelle Werte aus /net_config)
- Beim Ändern der Chat‑ID wird der lokale Post‑Cache gelöscht (damit alte Chatnachrichten nicht zum neuen Chat gehören).
- Datenschutz: Bot‑Token sollten vertraulich behandelt werden. Im Log werden Tokens standardmäßig nicht ausgegeben.

Kurzanleitung:
1. Setze Bot Token und Chat ID via `set net token ...` und `set net chat ...`.
2. Verbinde das Gerät mit WLAN (persistente Werte werden verwendet).
3. Der Server pusht Posts an Telegram und injiziert Telegram‑Nachrichten ins Mesh. Fotos resultieren in einem Kamerasymbol-Post.

---


## 🚀 Flash-Anleitung

1. Öffne [https://flasher.meshcore.co.uk](https://flasher.meshcore.co.uk)  
2. Wähle dein Gerät (z. B. Heltec, LilyGo T-Beam, RAK Wireless RAK4631)  
3. Scrolle nach unten und klicke auf **„Custom Firmware“**  
4. Lade die gewünschte `.bin`-Datei hoch (z. B. `ping_server.bin`)  
5. Nach dem Flashen:
   - Region: `EU/UK (NARROW)`  
   - Kanal: `Public` (+ optional `#ping`)  
   - Rolle wählen: `Companion`, `Repeater`, `Ping Server`, …  

---

## 🛠 Hardware-Kompatibilität

Bisher unterstützte Geräte:
- Heltec WiFi LoRa 32 V3
- LilyGo T-Beam 1.2

---

## 📜 Lizenz

Dieses Projekt basiert auf  
[MeshCore (© Liam Cottle, MIT License)](https://github.com/meshcore-dev/MeshCore)  

Zusatzkomponenten (z. B. `Ping Server`) sind ebenfalls unter der **MIT-Lizenz** veröffentlicht.  
Alle übrigen Teile bleiben quell- und protokollkompatibel zum Original.

---

## 💬 Community & Support

- 📖 Willkommensseite: [meshcore.bremesh.de/moin](https://meshcore.bremesh.de/moin)  
- 🗺️ Karten:  
  - MeshCore-Karte → [meshcore.bremesh.de](https://meshcore.bremesh.de)  
  - Meshtastic-Karte → [meshtastic.bremesh.de](https://meshtastic.bremesh.de)
