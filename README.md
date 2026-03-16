# ESP32 Zisternen-Monitor

Automatisches Zisternen-Überwachungssystem mit ESP32, TFT-Display und automatischer Pumpensteuerung.

## Features

- **Wasserstandsmessung** mit MPX5050 Drucksensor
- **TFT-Display** (480x320, ST7796) zur Echtzeitanzeige
- **Touch-Steuerung** mit XPT2046 Touch-Controller für manuelle Pumpensteuerung
- **Automatische Pumpensteuerung** mit Hysterese
- **Intelligente Pumpenüberwachung** mit Lernphase und Alarm bei Anomalien
- **Automatischer Druckausgleich** mit Luftpumpe (alle 5 Min. für 10 Sek.)
- Messung wird während Druckausgleich pausiert
- **ESP-NOW Datenübertragung** zur Wetterstation (alle 15 Minuten)
- **RRD-Verlaufsdiagramm** (Round Robin Database) mit 96 Datenpunkten (24h Historie)
- **Visueller Fortschrittsbalken** für Füllstand
- Serielle Ausgabe für Monitoring

## Hardware

### Komponenten
- ESP32 DevKit v1
- 3.5" TFT Display (480x320, ST7796 Treiber) mit XPT2046 Touch-Controller
- MPX5050 Drucksensor (0-50 kPa)
- N-Channel MOSFET für Relais-Steuerung
- Relais für Wasserpumpe

### Pin-Belegung

| Komponente | Pin | Beschreibung |
|------------|-----|--------------|
| TFT MOSI | GPIO 13 | SPI Data |
| TFT MISO | GPIO 12 | SPI Data |
| TFT SCLK | GPIO 14 | SPI Clock |
| TFT CS | GPIO 15 | Chip Select |
| TFT DC | GPIO 2 | Data/Command |
| TFT RST | GPIO 4 | Reset |
| TFT BL | GPIO 27 | Backlight |
| Touch CS | GPIO 33 | Touch Chip Select (XPT2046) |
| Drucksensor | GPIO 35 | ADC1 CH7 |
| Pumpen-MOSFET | GPIO 17 | Digital Output |
| Luftpumpen-MOSFET | GPIO 16 | Digital Output |

## Funktionsweise

### Wasserstandsmessung
- Der MPX5050 Drucksensor misst den hydrostatischen Druck
- ADC-Werte (0-4095) werden in cm Wasserstand umgerechnet
- **Mittelwertbildung aus 100 Messungen** für sehr stabile Werte (reduziert Sensor-Schwankungen)
- **Display-Update alle 3 Sekunden** (reduziert Flackern, zeigt gleichmäßigere Werte)

### Pumpensteuerung

Die Pumpe kann in drei Modi betrieben werden:

#### 1. **AUTO-Modus** (Standard)
Automatische Steuerung mit **Hysterese**:
- **EINschalten**: bei ≥ 30 cm Wasserstand
- **AUSschalten**: bei ≤ 15 cm Wasserstand

Dies verhindert häufiges Ein-/Ausschalten.

#### 2. **MANUELL EIN-Modus**
- Pumpe läuft kontinuierlich bis der Wasserstand 15 cm erreicht
- Wechselt dann automatisch zurück in den AUTO-Modus
- Nützlich zum schnellen Entleeren der Zisterne

#### 3. **MANUELL AUS-Modus**
- Pumpe bleibt ausgeschaltet, unabhängig vom Wasserstand
- Nützlich für Wartungsarbeiten

#### Moduswechsel:
- **Touch-Display**: Tippen Sie auf den farbigen Button auf dem Display
  - 🔵 **Blau** = AUTO-Modus
  - 🟢 **Grün** = MANUELL EIN  
  - 🔴 **Rot** = MANUELL AUS
- **Serielle Konsole**: Sende 'M' über die serielle Konsole (115200 Baud)
  
**Touch-Controller**: XPT2046 (über SPI, Chip Select GPIO 33)
- Die Touch-Kalibrierung kann in `main.cpp` in der Funktion `checkTouchButton()` angepasst werden
- Bei ersten Tests werden die Touch-Koordinaten im Serial Monitor angezeigt
- Falls der Touch nicht genau funktioniert, passen Sie die `map()`-Werte an

### Intelligente Pumpenüberwachung

Das System überwacht automatisch die Pumpenlaufzeit und erkennt Probleme (z.B. verstopfter Filter, defekte Pumpe).

#### Funktionsweise

**1. Lernphase (erste 3 Pumpläufe):**
- Bei den ersten 3 automatischen Pumpläufen wird die Laufzeit gemessen
- Aus diesen 3 Messungen wird ein **Mittelwert** (Referenzzeit) berechnet
- Die Referenzzeit wird **dauerhaft im Flash-Speicher** gespeichert (überlebt Neustarts)
- Status wird über serielle Konsole ausgegeben

**2. Überwachungsphase (ab 4. Pumplauf):**
- Jede weitere Pumpenlaufzeit wird mit der Referenzzeit verglichen
- **Alarm-Schwelle**: 150% der Referenzzeit
- Bei Überschreitung wird ein **Alarm** ausgelöst

**3. Alarm-Benachrichtigung:**
- 🔴 **Blinkendes Display** (alle 500ms) bei aktivem Alarm
- 📡 **Sofortige ESP-NOW Nachricht** an die Wetterstation
- 💾 **Alarm-Status** wird im ESP-NOW Datenstrom mitgesendet

#### Beispiel
- Referenzzeit: 120 Sekunden (Mittelwert aus ersten 3 Läufen)
- Alarm-Schwelle: 180 Sekunden (150% von 120s)
- Wenn Pumpe > 180 Sekunden läuft → **ALARM**

#### Alarm zurücksetzen
Der Alarm wird automatisch zurückgesetzt, wenn:
- Die nächste Pumpenlaufzeit wieder im normalen Bereich liegt
- Das System neu gestartet wird (Referenzzeit bleibt erhalten)

#### Konfiguration

```cpp
#define PUMP_ALARM_THRESHOLD 1.5     // 150% der Referenzzeit
#define ALARM_BLINK_INTERVAL 500     // Blink-Intervall in ms
```

**Hinweis:** Die Überwachung funktioniert nur im **AUTO-Modus**, da nur dort die Pumpe vom Wasserstand gesteuert wird.

### RRD-Verlaufs-Graph
- **Ringpuffer** mit 96 Datenpunkten (24 Stunden Historie)
- **Sampling-Intervall**: 15 Minuten pro Datenpunkt
- **Y-Achse**: 15-30 cm (relevanter Bereich für Pumpensteuerung)
- **X-Achse**: Stunden-Beschriftung (0h bis 24h)
- **Gitterlinien** bei 15, 20, 25, 30 cm
- **Grüne Linie**: Pumpe EIN-Schwelle (30 cm)
- **Rote Linie**: Pumpe AUS-Schwelle (15 cm)
- **Cyan-Kurve**: Aktueller Wasserstandverlauf

Der älteste Wert wird durch den neuesten überschrieben (Round Robin).

### Automatischer Druckausgleich
- **Luftpumpe** läuft automatisch alle **5 Minuten** für **10 Sekunden**
- Hält den Druck im Messrohr konstant
- **Messung wird pausiert** während die Luftpumpe läuft
- Verhindert Fehlmessungen durch Druckschwankungen
- Countdown bis zur nächsten Aktivierung wird angezeigt

### ESP-NOW Datenübertragung
- **Drahtlose Kommunikation** ohne WLAN-Router (direkte ESP-zu-ESP Verbindung)
- Sendet alle **15 Minuten** automatisch:
  - Wasserstand in cm
  - Roher ADC-Wert
  - Pumpenstatus (EIN/AUS)
  - **Pumpen-Alarm-Status** (bei Laufzeitüberschreitung)
- **Sofortige Alarm-Nachricht** bei Erkennung einer Pumpenstörung
- **Reichweite**: bis zu 200m im Freien
- **Niedriger Stromverbrauch**
- Status-Anzeige auf Display (OK/FEHLER, Sendezähler)
- Countdown bis zur nächsten Übertragung

**Setup:** Siehe [ESPNOW_SETUP.md](ESPNOW_SETUP.md) für Konfiguration

## Kalibrierung

Passe die Werte in `src/main.cpp` an:

```cpp
#define ADC_MIN 0              // ADC-Wert bei leerem Tank
#define ADC_MAX 4095           // ADC-Wert bei vollem Tank
#define WATER_LEVEL_MAX 300    // Maximale Füllhöhe in cm

#define PUMP_ON_LEVEL 30.0     // Pumpe EIN-Schwelle
#define PUMP_OFF_LEVEL 15.0    // Pumpe AUS-Schwelle
```

**Graph-Einstellungen:**
```cpp
#define GRAPH_MIN 15.0         // Min. Wasserstand im Graph
#define GRAPH_MAX 30.0         // Max. Wasserstand im Graph
#define GRAPH_SAMPLES 96       // Anzahl Datenpunkte
#define SAMPLE_INTERVAL 900000 // Messintervall in ms (15 Minuten)
#define DISPLAY_UPDATE_INTERVAL 3000 // Display-Aktualisierung (3 Sekunden)
```

**Zeitspanne des Graphen:**
- 96 Punkte × 15 Minuten = 1440 Minuten = **24 Stunden Historie**

**Luftpumpen-Einstellungen:**
```cpp
#define AIR_PUMP_INTERVAL 300000  // 5 Minuten zwischen Aktivierungen
#define AIR_PUMP_DURATION 10000   // 10 Sekunden Laufzeit
```

**ESP-NOW Einstellungen:**
```cpp
#define ESPNOW_SEND_INTERVAL 900000  // 15 Minuten (in ms)

// MAC-Adresse der Wetterstation anpassen!
uint8_t weatherStationMAC[] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
```

Siehe [ESPNOW_SETUP.md](ESPNOW_SETUP.md) für vollständige Anleitung.

## Installation

### PlatformIO
```bash
git clone https://github.com/doctorPit2/ESP32_Cisterne.git
cd ESP32_Cisterne
pio run --target upload
```

### Bibliotheken
- TFT_eSPI (v2.5.43)
- ThingPulse XPT2046 Touch (v1.4)

## Wartung & Troubleshooting

### Pumpenüberwachung zurücksetzen

Wenn Sie die Pumpe ausgetauscht oder repariert haben:

1. **Referenzzeit löschen** (über serielle Konsole):
```cpp
// Im Setup() einmalig hinzufügen:
preferences.begin("pumpMonitor", false);
preferences.clear();  // Alle gespeicherten Werte löschen
preferences.end();
```

2. **Neustart** des ESP32
3. Die **Lernphase startet automatisch** neu (erste 3 Pumpläufe)

### Serielle Überwachung

Öffnen Sie den Serial Monitor (115200 Baud) für detaillierte Informationen:
- ADC-Werte und Wasserstand
- Pumpenschaltungen mit GPIO-Status
- Lernphase-Fortschritt (1/3, 2/3, 3/3)
- Referenzzeit-Berechnung
- Pumpen-Alarm mit Details
- ESP-NOW Übertragungsstatus
- Touch-Koordinaten (für Kalibrierung)

### Häufige Probleme

**Pumpen-Alarm trotz funktionierender Pumpe:**
- Wasserstand in Zisterne ist niedriger als erwartet
- Zulauf zur Zisterne wurde geändert
- → Referenzzeit neu lernen lassen (siehe oben)

**Touch-Button reagiert nicht:**
- Touch-Kalibrierung in `main.cpp` anpassen
- Aktivieren Sie `TOUCH_CALIBRATION_MODE` temporär
- Koordinaten im Serial Monitor ablesen

**ESP-NOW sendet nicht:**
- MAC-Adresse der Wetterstation prüfen
- Reichweite (max. 200m) beachten
- Status auf Display kontrollieren

## Sicherheitshinweise

⚠️ **Wichtig:**
- Der ESP32 ADC verträgt maximal **3,3V**
- Falls der MPX5050 höhere Spannungen ausgibt, Spannungsteiler verwenden
- MOSFET-Schaltung auf korrekte Dimensionierung prüfen
- Pumpe nie trocken laufen lassen
- **Pumpenüberwachung beachten**: Bei Alarm die Pumpe und Filter prüfen
- Regelmäßig die ESP-NOW Verbindung zur Wetterstation kontrollieren

## Display-Ansicht

**Normal-Betrieb:**
```
┌───────────────────────────────────────────────────────────┐
│ Zisternen-Monitor                                         │
│                                                            │
│ ADC-Wert:              │   Verlauf (15min/Punkt)          │
│   1234 / 4095          │  30├─────────────────────────    │
│                        │    │         ╱╲                  │
│ Wasserstand:           │  25│      ╱─╯  ╲                 │
│   25.3 cm              │    │    ╱        ╲               │
│                        │  20│  ╱           ╲─╮            │
│ [████████░░░]          │    │╱                ╲           │
│                        │  15└──────────────────────╲──    │
│ [    AUTO-MODUS   ]    │         ← 24h    0h →            │
│ Pumpe: EIN             │                                  │
│ Luftpumpe: AUS         │                                  │
│ ESP-NOW: ✓ OK #12      │                                  │
│ Nächste ESP-NOW: 14m   │                                  │
│ Nächste Luftp.: 4m 23s │                                  │
└───────────────────────────────────────────────────────────┘
```

**Lernphase (erste 3 Pumpläufe):**
```
│ Überwachung: LERN 2/3  │  ← Zeigt Fortschritt
```

**Alarm-Modus (rotes Blinken):**
```
┌───────────────────────────────────────────────────────────┐
│                    🚨 PUMPEN-ALARM! 🚨                    │
│                                                            │
│ Laufzeit überschritten!                                   │
│ → Filter prüfen                                           │
│ → Pumpenleistung prüfen                                   │
└───────────────────────────────────────────────────────────┘
```

**Layout:**
- **Links**: Aktuelle Werte (ADC, Wasserstand, Pumpe, Luftpumpe, ESP-NOW, Countdowns)
- **Rechts**: 24h-Verlaufs-Graph mit Y-Achse (15-30 cm) und Zeitachse
- **Button**: Farbiger Modus-Button (Blau=AUTO, Grün=MANUELL EIN, Rot=MANUELL AUS)
- **Alarm**: Roter blinkender Bildschirm bei Pumpen-Störung

## Lizenz

MIT License

## Autor

doctorPit2
