# 🔄 BratenDreher - Smart Rotisserie Controller

Ein intelligenter Spießbraten-Dreher mit ESP32, NEMA 17 Stepper Motor und Bluetooth-Steuerung über eine moderne Web-App.

## 🎯 Features

- **FastAccelStepper + TMC2209**: Beste Kombination für flüssige Rotation mit Hardware-Beschleunigung
- **Realistische Geschwindigkeit**: 0.1 - 30 RPM (bis 0.5 RPS) für perfekte Bratenrotation
- **Bluetooth LE**: Drahtlose Steuerung über Web Bluetooth API
- **Web Interface**: Modernes, responsives UI für Smartphones und Tablets
- **Erweiterte Einstellungen**: Microsteps (8-256) und Motorstrom (10-100%) konfigurierbar
- **Statistiken**: Gesamtumdrehungen, Laufzeit und Durchschnittsgeschwindigkeit
- **Einstellungen speichern**: Preferences werden im Flash-Speicher gesichert
- **Emergency Stop**: Sofortige Notabschaltung mit Bluetooth-Verbindungsüberwachung

## 🛠️ Hardware

- **ESP32-S3**: Mikrocontroller mit Bluetooth LE
- **NEMA 17 Stepper**: 200 Schritte pro Umdrehung
- **1:10 Getriebe**: Untersetzung für präzise, langsame Drehung
- **FastAccelStepper**: Hardware-basierte Step-Generierung (ESP32 Timer)
- **TMC2209 Driver**: Professioneller Stepper-Treiber mit UART-Kommunikation
- **PD-Stepper Board**: Stepper-Treiber-Board mit ESP32

### Pin-Konfiguration (PD-Stepper Board)

```cpp
TMC_EN    = 21  // TMC2209 Enable
STEP_PIN  = 5   // Step-Signal  
DIR_PIN   = 6   // Richtung
TMC_TX    = 17  // UART TX zu TMC2209
TMC_RX    = 18  // UART RX von TMC2209
MS1       = 1   // Microstep 1
MS2       = 2   // Microstep 2
DIAG      = 16  // Diagnostic/Stall Pin
STATUS_LED = 2  // Status-LED
```

## 📁 Projektstruktur

```
BratenDreher/
├── src/
│   └── main.cpp                    # Hauptprogramm
├── lib/
│   ├── StepperController/          # Stepper Motor Klasse
│   │   ├── StepperController.h
│   │   └── StepperController.cpp
│   └── BLEManager/                 # Bluetooth LE Manager
│       ├── BLEManager.h
│       └── BLEManager.cpp
├── web/                            # Web Interface (für GitHub Pages)
│   ├── index.html
│   ├── style.css
│   └── script.js
├── examples/                       # Original Beispiele
└── platformio.ini                 # PlatformIO Konfiguration
```

## 🔧 Setup

### 1. PlatformIO Installation

```bash
# PlatformIO CLI installieren
curl -fsSL https://raw.githubusercontent.com/platformio/platformio-core-installer/master/get-platformio.py -o get-platformio.py
python3 get-platformio.py
```

### 2. Projekt kompilieren

```bash
cd BratenDreher
pio run
```

### 3. Auf ESP32 flashen

```bash
pio run -t upload
```

### 4. Serial Monitor

```bash
pio device monitor
```

## 📱 Web Interface

Das Web-Interface befindet sich im `web/` Ordner und kann über GitHub Pages gehostet werden.

### GitHub Pages Setup

1. Repository auf GitHub erstellen
2. Web-Dateien in `docs/` Ordner verschieben oder direkt aus `main` Branch deployen
3. GitHub Pages in Repository-Einstellungen aktivieren
4. URL: `https://[username].github.io/BratenDreher/`

### Lokaler Test

```bash
cd web
python3 -m http.server 8000
# Dann öffne http://localhost:8000
```

## 🔗 Bluetooth LE GATT Profile

### Service UUID
```
12345678-1234-1234-1234-123456789abc
```

### Characteristics

| Characteristic | UUID | Typ | Beschreibung |
|---|---|---|---|
| Speed | ...ab1 | R/W | Geschwindigkeit (0.1-30 RPM) |
| Direction | ...ab2 | R/W | Richtung (1=CW, 0=CCW) |
| Enable | ...ab3 | R/W | Motor Ein/Aus (1=Ein, 0=Aus) |
| Status | ...ab4 | R/N | JSON Status-Updates |
| Microsteps | ...ab5 | R/W | Microsteps (8, 16, 32, 64, 128, 256) |
| Current | ...ab6 | R/W | Motorstrom (10-100%) |
| Reset | ...ab7 | W | Statistiken zurücksetzen (1=Reset) |

### Status JSON Format

```json
{
  "enabled": true,
  "speed": 15.0,
  "direction": "cw",
  "running": true,
  "connected": true,
  "totalRevolutions": 25.234,
  "runtime": 3024,
  "microsteps": 32,
  "current": 60,
  "timestamp": 123456
}
```

## 🎮 Bedienung

### Web App

1. **Verbinden**: "Connect" Button drücken, "BratenDreher" auswählen
2. **Motor starten**: Toggle-Switch auf "ON"
3. **Geschwindigkeit**: Slider (0.1-30 RPM) oder Preset-Buttons
4. **Richtung**: Clockwise/Counter-clockwise Buttons
5. **Erweiterte Einstellungen**: 
   - Microsteps: 8, 16, 32, 64, 128, 256
   - Motorstrom: 10-100%
6. **Statistiken**: Gesamtumdrehungen, Laufzeit, Durchschnittsgeschwindigkeit
7. **Reset**: Statistiken zurücksetzen
8. **Stop**: Toggle auf "OFF" oder Emergency Stop

### Status-LED (Pin 2)

- **Blinkt langsam**: Wartet auf Verbindung
- **Leuchtet konstant**: Verbunden
- **Blinkt schnell**: Fehler beim Initialisieren

## 🔄 Motorspezifikationen

- **NEMA 17**: 200 Schritte/Umdrehung
- **FastAccelStepper**: Hardware-Timer basierte Step-Generierung (interrupt-sicher)
- **TMC2209 Driver**: Professioneller Treiber mit bis zu 256 Microsteps
- **Getriebe**: 1:10 Untersetzung
- **Gesamt**: 2000 Schritte/Umdrehung (bei 1 Microstep)
- **Endgeschwindigkeit**: 0.1 - 30 RPM (bis 0.5 RPS) - realistisch für Grillgut
- **Drehmoment**: Sehr hoch durch Untersetzung (ideal für schwere Braten bis 10kg)

### Warum FastAccelStepper + TMC2209?

- **Hardware-Timer**: ESP32 Timer generiert Steps - läuft auch bei WiFi/BLE-Unterbrechungen flüssig
- **Beschleunigungsrampen**: Sanftes Anfahren und Bremsen, kein Stepverlust
- **TMC2209 Integration**: Perfekte Kombination für leisen, präzisen Betrieb
- **Interrupt-sicher**: Keine Geschwindigkeitsschwankungen durch andere Tasks

### Geschwindigkeitsberechnung

```cpp
// Für 15 RPM Ausgangswelle (realistische Grillgeschwindigkeit):
// Motorgeschwindigkeit = 15 * 10 = 150 RPM
// Bei 32 Microsteps:
motor_steps_per_second = (150 * 200 * 32) / 60 = 16000 steps/s
// FastAccelStepper: stepper->setSpeedInHz(16000)

// Resultat: Perfekt flüssige Rotation auch bei ESP32-Last
```

## 🛡️ Sicherheit

- **Automatische Abschaltung** bei Bluetooth-Verbindungsabbruch
- **Emergency Stop** Button für sofortige Abschaltung
- **Geschwindigkeitsbegrenzung** auf sinnvolle Werte
- **Auto-Enable** mit konfigurierbaren Delays

## 🧪 Testing

### Hardware Test

```cpp
// FastAccelStepper Test
stepper->setSpeedInHz(1000);    // 1000 steps/s
stepper->setAcceleration(500);  // Smooth acceleration  
stepper->runForward();          // Start continuous rotation
delay(60000);                   // 1 Minute
stepper->stopMove();            // Smooth stop
```

### BLE Test

```javascript
// Web Console Test
await navigator.bluetooth.requestDevice({
  filters: [{ name: 'BratenDreher' }]
});
```

## 📚 Abhängigkeiten

- **FastAccelStepper**: Hardware-Timer basierte Step-Generierung für ESP32
- **TMC2209**: Stepper Driver Library von Janelia für UART-Kommunikation
- **ArduinoJson**: JSON Parsing für BLE-Kommunikation
- **ESP32 BLE**: Bluetooth Low Energy Stack
- **Preferences**: ESP32 Flash-Speicher für Einstellungen
- **Web Bluetooth API**: Browser-seitige BLE-Unterstützung

## 🔮 Zukünftige Erweiterungen

- [ ] Timer-Funktion (automatisches Stoppen nach Zeit)
- [ ] Temperatur-Sensor Integration (PT100/PT1000)
- [ ] Programmierbare Drehmuster (Pendel-Bewegung)
- [ ] WiFi-Backup Kommunikation
- [ ] Mobile App (React Native/Flutter)
- [ ] Mehrere Motoren gleichzeitig (Hauptgrill + Beilagen)
- [ ] Cloud-Logging und Rezept-Management
- [ ] Lastüberwachung via TMC2209 Stall Detection

## ⚠️ Troubleshooting

### Motor dreht nicht
- Stromversorgung prüfen
- Kabelverbindungen prüfen
- Serial Monitor für Fehler checken

### BLE verbindet nicht
- Browser-Kompatibilität (Chrome/Edge erforderlich)
- Bluetooth am Gerät aktiviert?
- ESP32 in Reichweite?

### Web Interface lädt nicht
- HTTPS erforderlich für Web Bluetooth
- Lokaler Server für Tests nutzen

## 📄 Lizenz

MIT License - Siehe LICENSE Datei für Details

## 👨‍💻 Entwicklung

Entwickelt für PlatformIO mit ESP32. Nutzt moderne C++ Klassen-Architektur für einfache Erweiterbarkeit.

**Hauptklassen:**
- `StepperController`: Hardware-Abstraktion
- `BLEManager`: Kommunikation
- `main.cpp`: Koordination

---

**Happy Grilling! 🔥🥩**
