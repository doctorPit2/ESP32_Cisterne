# ESP32 Zisternen-Monitor

Automatisches Zisternen-Überwachungssystem mit ESP32, TFT-Display und automatischer Pumpensteuerung.

## Features

- **Wasserstandsmessung** mit MPX5050 Drucksensor
- **TFT-Display** (480x320, ST7796) zur Echtzeitanzeige
- **Touch-Steuerung** mit XPT2046 Touch-Controller für manuelle Pumpensteuerung
- **Automatische Pumpensteuerung** mit Hysterese
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
  - Pumpenstatus
  - Betriebszeit
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

## Sicherheitshinweise

⚠️ **Wichtig:**
- Der ESP32 ADC verträgt maximal **3,3V**
- Falls der MPX5050 höhere Spannungen ausgibt, Spannungsteiler verwenden
- MOSFET-Schaltung auf korrekte Dimensionierung prüfen
- Pumpe nie trocken laufen lassen

## Display-Ansicht

```
┌───────────────────────────────────────────────────────────┐
│ Zisternen-Monitor                                         │
│                                                            │
│ ADC-Wert:              │   Verlauf (5s/Punkt)             │
│   1234 / 4095          │  30├─────────────────────────    │
│                        │    │         ╱╲                  │
│ Wasserstand:           │  25│      ╱─╯  ╲                 │
│   25.3 cm              │    │    ╱        ╲               │
│                        │  20│  ╱           ╲─╮            │
│ [████████░░░]          │    │╱                ╲           │
│                        │  15└──────────────────────╲──    │
│ Pumpe: AUS             │         ← Ältere   Neue →        │
│ Luftpumpe: AUS         │                                  │
│ ESP-NOW: OK #12        │                                  │
│ EIN: 30cm AUS: 15cm    │                                  │
│ Nächste ESP-NOW: 14m 23s                                  │
│ Nächste Luftp.: 4m 23s │                                  │
└───────────────────────────────────────────────────────────┘
```

**Layout:**
- **Links**: Aktuelle Werte (ADC, Wasserstand, Pumpe, Luftpumpe, ESP-NOW, Countdowns)
- **Rechts**: Verlaufs-Graph mit Y-Achse (15-30 cm) und Zeitachse

## Lizenz

MIT License

## Autor

doctorPit2
