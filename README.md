# 🔄 BratenDreher - Smart Rotisserie Controller

Ein intelligenter Spießbraten-Dreher auf Basis des [PD-Stepper](https://github.com/Thingsbyjosh/PD-Stepper)
Boards (ESP32-S3 + TMC2209 + USB-C Power Delivery), gesteuert über Bluetooth LE
und eine Web-App.

## 🎯 Features

- **FastAccelStepper + TMC2209**: Hardware-Timer-basierte Step-Erzeugung, flüssig auch unter BLE-Last
- **Geschwindigkeit**: 0.1 - 30 RPM an der Abtriebswelle
- **Variable Geschwindigkeit**: Positionsabhängige Modulation, um ungleichmäßiges Bräunen auszugleichen
- **USB-C Power Delivery**: Aushandlung von 5/9/12/15/20 V über den CH224K, inkl. Auto-Negotiation
- **Bluetooth LE**: Steuerung über die Web Bluetooth API, MsgPack als Protokoll
- **StallGuard**: Lastüberwachung und Stall-Erkennung über den TMC2209
- **Installierbare Web-App**: PWA mit Offline-Support, auf Android per Chrome installierbar
- **Persistente Einstellungen**: Geschwindigkeit, Richtung, Strom, Beschleunigung und StallGuard-Schwelle im Flash
- **Statistik**: Umdrehungen und reine Motorlaufzeit (Standzeiten zählen nicht mit), daraus die Durchschnittsgeschwindigkeit
- **OTA-Update**: WLAN-Update per Tastendruck beim Booten

## 🛠️ Hardware

Dieses Projekt läuft auf dem **PD-Stepper V1.1** Board - nicht auf einem
ESP32-S3-Devkit. Die vollständige Pinbelegung steht in
[lib/BoardPins/BoardPins.h](lib/BoardPins/BoardPins.h); dort ist auch
dokumentiert, warum `LED_BUILTIN` hier nicht verwendet werden darf (GPIO 48 ist
auf diesem Board CFG2 des PD-Triggers).

| Funktion | GPIO |
|---|---|
| TMC_EN / STEP / DIR | 21 / 5 / 6 |
| MS1 / MS2 | 1 / 2 |
| SPREAD (LOW = StealthChop) | 7 |
| TMC UART TX / RX | 17 / 18 |
| DIAG (StallGuard) | 16 |
| PD: PG / CFG1 / CFG2 / CFG3 | 15 / 38 / 48 / 47 |
| VBUS Messung | 4 |
| LED1 / LED2 | 10 / 12 |
| Taster SW1 / SW2 / SW3 | 35 / 36 / 37 |

### Motorkonfiguration

- **NEMA 17**: 200 Schritte/Umdrehung
- **Getriebe**: 1:10 Untersetzung
- **Microsteps**: fest auf 16 eingestellt (`MICRO_STEPS` in `StepperController.h`)
- **Gesamt**: 32000 Microsteps pro Abtriebsumdrehung

## 🔧 Setup

### 1. Zugangsdaten anlegen

```bash
cp include/secrets.h.example include/secrets.h
# WIFI_SSID, WIFI_PASSWORD und OTA_PASSWORD eintragen
```

`include/secrets.h` ist gitignored und darf nicht eingecheckt werden. Die Daten
werden ausschließlich für den OTA-Modus benötigt - im Normalbetrieb läuft kein WLAN.

### 2. Bauen und flashen

```bash
pio run                                    # Normaler Build
pio run -t upload                          # Über USB flashen
pio device monitor                         # Serieller Monitor

pio run -e esp32-s3-devkitm-1-debug        # Build mit ausführlichem dbg_* Logging
pio run -e esp32-s3-devkitm-1-ota -t upload  # Update über WLAN
```

Es gibt zwei Log-Ebenen (siehe [lib/dbg_print/dbg_print.h](lib/dbg_print/dbg_print.h)):
`info_*` ist immer aktiv, `dbg_*` nur im `-debug` Environment.

### 3. OTA-Update

SW1 (GPIO 35) beim Booten gedrückt halten. Das Board verbindet sich dann mit dem
WLAN, meldet sich als `BratenDreher.local` und wartet auf ein Update - die
Motorsteuerung startet in diesem Modus nicht.

## 📱 Web Interface

Das Interface liegt in [web/](web/) und wird per GitHub Actions nach GitHub Pages
deployed (siehe [.github/workflows/static.yml](.github/workflows/static.yml)).

Web Bluetooth benötigt HTTPS (oder `localhost`) sowie einen Chromium-basierten Browser.

```bash
python3 -m http.server 8000 --directory web
# http://localhost:8000
```

### Installation als App (Android)

Die Seite ist eine Progressive Web App und lässt sich auf Android über Chrome
per „Zum Startbildschirm hinzufügen" installieren. Sie startet dann ohne
Browser-UI und funktioniert dank Service Worker auch ohne Netzverbindung -
praktisch am Grill, wo die Verbindung zum Gerät ohnehin über BLE läuft.

> **iOS wird nicht unterstützt.** Alle iOS-Browser müssen WebKit verwenden, und
> WebKit implementiert die Web Bluetooth API nicht. Die App ließe sich zwar
> installieren, könnte sich aber nicht mit dem BratenDreher verbinden. Fehlt
> Web Bluetooth, zeigt die Seite darum einen entsprechenden Hinweis an.

Der Service Worker ([web/sw.js](web/sw.js)) cached die komplette App und wird
über einen Cache-Namen versioniert. Beim Deploy ersetzt der Workflow den
Platzhalter `__CACHE_VERSION__` durch den Commit-SHA, sodass jede neue Version
sauber übernommen wird - lokal bleibt der Platzhalter stehen, was für Tests
genügt. Eine neue Version wird erst aktiv, wenn die App vollständig geschlossen
und neu geöffnet wurde; damit reißt ein Update nie eine laufende BLE-Verbindung ab.

## 🔗 Bluetooth LE Protokoll

Es gibt **eine** Characteristic, über die in beide Richtungen kommuniziert wird.
Die Nutzdaten sind MsgPack-kodiert.

| | UUID |
|---|---|
| Service | `12345678-1234-1234-1234-123456789abc` |
| Command Characteristic (R/W/Notify) | `12345678-1234-1234-1234-123456789ab1` |

### Kommandos (App → Gerät)

Format: `{ "type": <kürzel>, "value": <wert> }`

| Kürzel | Wert | Bedeutung |
|---|---|---|
| `ss` | float | Sollgeschwindigkeit in RPM (0.1-30) |
| `sd` | bool | Richtung (true = im Uhrzeigersinn) |
| `en` | bool | Motor ein/aus |
| `es` | - | Emergency Stop (wird bevorzugt behandelt) |
| `sc` | int | Motorstrom in % (10-100) |
| `sa` | int | Beschleunigung in steps/s² (100-100000) |
| `svs` | float | Stärke der Geschwindigkeitsmodulation (0.0-1.0) |
| `svp` | float | Phasenversatz in Radiant |
| `sve` | bool | Modulation ein/aus |
| `st` | int | StallGuard-Schwelle (0-255) |
| `stv` | int | PD-Zielspannung (5, 9, 12, 15 oder 20) |
| `anh` | - | Höchste verfügbare PD-Spannung automatisch aushandeln |
| `rc` | - | Statistik zurücksetzen |
| `rs` | - | Stall-Zähler zurücksetzen |
| `ras` | - | Alle aktuellen Statuswerte anfordern |

### Statusmeldungen (Gerät → App)

Format: `{ "type": "status_update", <kürzel>: <wert>, ... }`. Mehrere Werte
werden zu einer Nachricht zusammengefasst, begrenzt durch die ausgehandelte MTU.

| Kürzel | Bedeutung |
|---|---|
| `sp` / `cs` | Soll- / Istgeschwindigkeit (RPM) |
| `dir` | Richtung (`"cw"` / `"ccw"`) |
| `en` | Motor aktiv |
| `cur` | Motorstrom (%) |
| `acc` | Beschleunigung (steps/s²) |
| `sve` / `svs` / `svp` | Modulation aktiv / Stärke / Phase |
| `ca` | Aktueller Drehwinkel (Radiant) |
| `tr` / `rt` | Umdrehungen gesamt / reine Laufzeit des Motors (ms) |
| `sd` / `sc` | Stall erkannt / Stall-Zähler |
| `sgt` / `sgr` | StallGuard-Schwelle / -Messwert |
| `tmcst` / `tmct` | TMC2209 Kommunikation / Temperaturstufe (0-4) |
| `pdns` / `pdnv` / `pdcv` / `pdpg` | PD Status / ausgehandelte / gemessene Spannung / Power Good |

Warnungen und Fehler kommen als `{ "type": "notification", "level": ..., "message": ... }`.

## 🏗️ Architektur

Drei FreeRTOS-Tasks kommunizieren ausschließlich über Queues - kein Task greift
direkt auf den Zustand eines anderen zu.

```
  BLEManager  ──sendCommand()──►  SystemCommand  ──getCommand()──►  StepperController
   (Core 0)                                                            (Core 1)
                                                     └──────────►  PowerDeliveryTask
                                                                      (Core 1)
       ▲                                                                  │
       └────────getStatusUpdate()──  SystemStatus  ◄──publishStatusUpdate()┘
```

- [lib/Task/](lib/Task/) - schlanke Basisklasse für FreeRTOS-Tasks
- [lib/BoardPins/](lib/BoardPins/) - zentrale Pinbelegung
- [lib/SystemCommand/](lib/SystemCommand/) - Kommando-Queues (App → Hardware)
- [lib/SystemStatus/](lib/SystemStatus/) - Status- und Notification-Queues (Hardware → App)
- [lib/StepperController/](lib/StepperController/) - Motorsteuerung, StallGuard, Speed-Modulation
- [lib/PowerDeliveryTask/](lib/PowerDeliveryTask/) - PD-Aushandlung und Spannungsüberwachung
- [lib/BLEManager/](lib/BLEManager/) - GATT-Server und MsgPack-Protokoll

Der Stepper-Task wartet beim Start bis zu 10 s auf eine abgeschlossene
PD-Aushandlung und läuft danach auch ohne PD-Netzteil weiter.

## 🛡️ Sicherheit

- **Emergency Stop** bremst mit maximaler Rampe und überholt dabei wartende Kommandos
- **Geschwindigkeit und Beschleunigung** werden auf sinnvolle Bereiche begrenzt
- **Übertemperatur** des TMC2209 wird gemeldet (ab 120 °C, kritisch ab 157 °C)
- **OTA** ist passwortgeschützt

> **Hinweis:** Beim Trennen der BLE-Verbindung läuft der Motor bewusst weiter,
> damit ein kurzer Verbindungsabbruch den Bratvorgang nicht unterbricht. Zum
> Stoppen muss der Motor explizit ausgeschaltet werden.

## 🔮 Mögliche Erweiterungen

- [ ] Timer-Funktion (automatisches Stoppen nach Zeit)
- [ ] Temperatur-Sensor (der NTC-Eingang teilt sich GPIO 7 mit SPREAD und ist derzeit ungenutzt)
- [ ] AS5600-Encoder für echte Positionsrückmeldung

## ⚠️ Troubleshooting

**Motor dreht nicht** - Stromversorgung und PD-Status in der App prüfen; ist
`tmcst` auf "Error", antwortet der TMC2209 nicht über UART.

**BLE verbindet nicht** - Chromium-basierter Browser nötig, Seite muss über
HTTPS oder localhost laufen.

**Board bootet immer in den OTA-Modus** - SW1 klemmt oder wird beim Start gedrückt.

## 📄 Lizenz

MIT License - siehe LICENSE.

---

**Happy Grilling! 🔥🥩**
