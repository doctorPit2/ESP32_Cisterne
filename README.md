# ESP32 Zisternen-Monitor

Automatisches Zisternen-Überwachungssystem mit ESP32, TFT-Display und automatischer Pumpensteuerung.

## Features

- **Wasserstandsmessung** mit MPX5050 Drucksensor
- **TFT-Display** (480x320, ST7796) zur Echtzeitanzeige
- **Automatische Pumpensteuerung** mit Hysterese
- **Automatischer Druckausgleich** mit Luftpumpe (alle 5 Min. für 10 Sek.)
- Messung wird während Druckausgleich pausiert
- **RRD-Verlaufsdiagramm** (Round Robin Database) mit 220 Datenpunkten
- **Visueller Fortschrittsbalken** für Füllstand
- Serielle Ausgabe für Monitoring

## Hardware

### Komponenten
- ESP32 DevKit v1
- 3.5" TFT Display (480x320, ST7796 Treiber)
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
| Drucksensor | GPIO 35 | ADC1 CH7 |
| Pumpen-MOSFET | GPIO 17 | Digital Output |
| Luftpumpen-MOSFET | GPIO 16 | Digital Output |

## Funktionsweise

### Wasserstandsmessung
- Der MPX5050 Drucksensor misst den hydrostatischen Druck
- ADC-Werte (0-4095) werden in cm Wasserstand umgerechnet
- Mittelwertbildung aus 10 Messungen für stabile Werte

### Pumpensteuerung
Die Pumpe wird automatisch gesteuert mit **Hysterese**:
- **EINschalten**: bei ≥ 30 cm Wasserstand
- **AUSschalten**: bei ≤ 15 cm Wasserstand

Dies verhindert häufiges Ein-/Ausschalten.

### RRD-Verlaufs-Graph
- **Ringpuffer** mit 220 Datenpunkten (ca. 18 Minuten Historie)
- **Sampling-Intervall**: 5 Sekunden pro Datenpunkt
- **Y-Achse**: 15-30 cm (relevanter Bereich für Pumpensteuerung)
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
#define GRAPH_SAMPLES 220      // Anzahl Datenpunkte
#define SAMPLE_INTERVAL 5000   // Messintervall in ms
```

**Zeitspanne des Graphen:**
- 220 Punkte × 5 Sekunden = 1100 Sekunden ≈ **18 Minuten Historie**

**Luftpumpen-Einstellungen:**
```cpp
#define AIR_PUMP_INTERVAL 300000  // 5 Minuten zwischen Aktivierungen
#define AIR_PUMP_DURATION 10000   // 10 Sekunden Laufzeit
```

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
│ EIN: 30cm AUS: 15cm    │                                  │
│ Nächste Luftp.: 4m 23s │                                  │
└───────────────────────────────────────────────────────────┘
```

**Layout:**
- **Links**: Aktuelle Werte (ADC, Wasserstand, Pumpe, Luftpumpe, Countdown)
- **Rechts**: Verlaufs-Graph mit Y-Achse (15-30 cm) und Zeitachse

## Lizenz

MIT License

## Autor

doctorPit2
