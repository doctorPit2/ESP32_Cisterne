#include <Arduino.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include <esp_now.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <Preferences.h>

// TFT Display initialisieren
TFT_eSPI tft = TFT_eSPI();

// XPT2046 Touch-Controller initialisieren
#define TOUCH_CS 33   // Touch Chip Select Pin
#define TOUCH_IRQ 36  // Touch Interrupt Pin (optional, verbessert die Erkennung)
XPT2046_Touchscreen touch(TOUCH_CS, TOUCH_IRQ);

// MAC-Adresse der Wetterstation (muss angepasst werden!)
// Format: {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF}
//uint8_t weatherStationMAC[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};


uint8_t weatherStationMAC[] = {0x14, 0x33, 0x5C, 0x38, 0xD5, 0xD4};   //Kiste schwarz

// MAC-Adresse des RSSI Monitors (für parallelen Empfang)
uint8_t rssiMonitorMAC[] = {0xB0, 0xCB, 0xD8, 0x02, 0xFD, 0x08};   //RSSI Monitor

// Datenstruktur für ESP-NOW Übertragung
typedef struct {
  float waterLevel;      // Wasserstand in cm
  int adcValue;          // Roher ADC-Wert
  bool pumpActive;       // Status der Wasserpumpe
  bool pumpAlarm;        // Pumpen-Alarm bei Überschreitung
  unsigned long pumpReferenceTime;  // Referenzzeit in Sekunden
  unsigned long lastPumpDuration;   // Letzte Pumpdauer in Sekunden
} WaterLevelData;

WaterLevelData dataToSend;

// Drucksensor MPX5050 am GPIO 35 (ADC1 CH7)
#define PRESSURE_SENSOR_PIN 35

// MOSFET für Pumpenrelais am GPIO 17
#define PUMP_MOSFET_PIN 17

// MOSFET für Luftpumpe (Druckausgleich) am GPIO 16
#define AIR_PUMP_PIN 16
#define AIR_PUMP_INTERVAL 300000  // 5 Minuten in ms
#define AIR_PUMP_DURATION 10000   // 10 Sekunden in ms

// ESP-NOW Einstellungen
//#define ESPNOW_SEND_INTERVAL 900000  // 15 Minuten in ms (900.000 ms)
#define ESPNOW_SEND_INTERVAL 2000  // 15 Minuten in ms (900.000 ms)
// Kalibrierungswerte (anpassen nach Bedarf)
#define ADC_MIN 0          // ADC-Wert bei 0 cm Wasserstand
#define ADC_MAX 4095       // ADC-Wert bei maximalem Wasserstand
#define WATER_LEVEL_MAX 300 // Maximale Füllhöhe in cm (3 Meter)

// Pumpensteuerung (Hysterese) - als Variablen für Einstellung via Slider
float pumpOnLevel = 30.0;   // Pumpe EINschalten bei 30 cm
float pumpOffLevel = 15.0;  // Pumpe AUSschalten bei 15 cm

// RRD Graph-Einstellungen
#define GRAPH_WIDTH 220      // Breite des Graphen in Pixel
#define GRAPH_HEIGHT 250     // Höhe des Graphen in Pixel
#define GRAPH_X 250          // X-Position des Graphen
#define GRAPH_Y 50           // Y-Position des Graphen
#define GRAPH_MIN 15.0       // Minimaler Wasserstand im Graph (cm)
#define GRAPH_MAX 30.0       // Maximaler Wasserstand im Graph (cm)
#define GRAPH_SAMPLES 96     // Anzahl der Datenpunkte (24h / 15min = 96)
#define SAMPLE_INTERVAL 900000 // Messintervall in ms (15 Minuten)
#define DISPLAY_UPDATE_INTERVAL 3000 // Display-Update alle 3 Sekunden

// Touch-Buttons für manuelle Pumpensteuerung und Seitenwechsel
#define BUTTON_X 10
#define BUTTON_Y 60
#define BUTTON_W 230
#define BUTTON_H 40

// Seitenwechsel-Button (unterschiedliche Positionen je Seite)
#define PAGE_BTN_MAIN_X 10
#define PAGE_BTN_MAIN_Y 230
#define PAGE_BTN_SETUP_X 360
#define PAGE_BTN_SETUP_Y 270
#define PAGE_BTN_W 110
#define PAGE_BTN_H 40

// Slider-Definitionen
#define SLIDER_X 120
#define SLIDER_Y_ON 80
#define SLIDER_Y_OFF 140
#define SLIDER_W 300
#define SLIDER_H 30
#define SLIDER_MIN 10.0
#define SLIDER_MAX 50.0

// Touch-Support aktivieren
#define TOUCH_ENABLED true
#define TOUCH_CALIBRATION_MODE false  // Kalibrierung abgeschlossen!

// Pumpenmodus-Enum
enum PumpMode {
  MODE_AUTO,       // Automatische Steuerung (Hysterese 30cm EIN, 15cm AUS)
  MODE_MANUAL_ON,  // Manuell EIN (läuft dauerhaft)
  MODE_MANUAL_OFF  // Manuell AUS (bleibt aus)
};

// Globale Variablen
bool pumpActive = false;
PumpMode pumpMode = MODE_AUTO;  // Startet im Automatikmodus
float graphData[GRAPH_SAMPLES]; // Ringpuffer für Messwerte
int graphIndex = 0;              // Aktueller Index im Ringpuffer
unsigned long lastSampleTime = 0; // Zeitpunkt der letzten Messung
unsigned long lastDisplayUpdate = 0; // Zeitpunkt des letzten Display-Updates

// Display-Seiten
enum DisplayPage {
  PAGE_MAIN,       // Hauptansicht: Monitor, Wasserstand, Status
  PAGE_SETTINGS    // Einstellungen: Parameter, Slider
};
DisplayPage currentPage = PAGE_MAIN;
bool sliderDragging = false;
int draggedSlider = 0; // 1 = ON_LEVEL, 2 = OFF_LEVEL

// Luftpumpen-Variablen
unsigned long lastAirPumpTime = 0;  // Letzter Start der Luftpumpe
bool airPumpActive = false;          // Status der Luftpumpe
unsigned long airPumpStartTime = 0;  // Startzeit der aktuellen Luftpumpen-Phase

// ESP-NOW Variablen
unsigned long lastESPNowSend = 0;    // Zeitpunkt der letzten ESP-NOW Übertragung
bool espnowInitialized = false;      // ESP-NOW Initialisierungsstatus
int espnowSendCount = 0;             // Anzahl gesendeter Nachrichten
bool lastSendSuccess = false;        // Status der letzten Übertragung

// Pumpüberwachungs-Variablen
Preferences preferences;             // Für dauerhaftes Speichern
unsigned long pumpStartTime = 0;     // Zeitpunkt Pumpe EIN
unsigned long pumpRunTimes[3] = {0, 0, 0}; // Array für erste 3 Messungen (in Sekunden)
int pumpRunCount = 0;                // Anzahl der Messungen
unsigned long pumpReferenceTime = 0; // Referenzzeit in Sekunden (Mittelwert)
unsigned long lastPumpDuration = 0;  // Letzte Pumpdauer in Sekunden
bool pumpAlarmActive = false;        // Alarm-Status
bool pumpAlarmBlink = false;         // Blink-Status für TFT
unsigned long lastAlarmBlink = 0;    // Zeitpunkt letztes Blinken
#define PUMP_ALARM_THRESHOLD 1.5     // 150% der Referenzzeit
#define ALARM_BLINK_INTERVAL 500     // Blink-Intervall in ms

// ESP-NOW Callback-Funktion (wird nach dem Senden aufgerufen)
void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  if (status == ESP_NOW_SEND_SUCCESS) {
    lastSendSuccess = true;
    Serial.println("ESP-NOW: Daten erfolgreich gesendet!");
  } else {
    lastSendSuccess = false;
    Serial.println("ESP-NOW: Fehler beim Senden!");
  }
}

// ESP-NOW initialisieren
void initESPNow() {
  // WiFi im Station-Modus starten (für ESP-NOW erforderlich)
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  WiFi.setSleep(false);  // Power-Save deaktivieren für bessere Reichweite
  
  // Long Range Mode aktivieren (802.11b/g/n + LR für maximale Reichweite)
  esp_wifi_set_protocol(WIFI_IF_STA, WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N | WIFI_PROTOCOL_LR);
  Serial.println("INFO: Long Range Mode aktiviert (802.11b/g/n/LR)");
  
  // Kanal auf 1 setzen für ESP-NOW
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);
  esp_wifi_set_promiscuous(false);
  Serial.println("INFO: ESP-NOW Kanal fest auf 1 gesetzt");
  
  // TX Power auf Maximum setzen
  WiFi.setTxPower(WIFI_POWER_19_5dBm);
  int8_t power;
  esp_wifi_get_max_tx_power(&power);
  Serial.printf("TX Power: %d (= %.1f dBm)\n", power, power * 0.25);
  
  Serial.print("ESP32 MAC-Adresse: ");
  Serial.println(WiFi.macAddress());
  
  // ESP-NOW initialisieren
  if (esp_now_init() != ESP_OK) {
    Serial.println("Fehler beim Initialisieren von ESP-NOW!");
    espnowInitialized = false;
    return;
  }
  
  Serial.println("ESP-NOW erfolgreich initialisiert");
  
  // Callback-Funktion registrieren
  esp_now_register_send_cb(OnDataSent);
  
  // Peer (Wetterstation) hinzufügen
  esp_now_peer_info_t peerInfo;
  memset(&peerInfo, 0, sizeof(peerInfo));
  memcpy(peerInfo.peer_addr, weatherStationMAC, 6);
  peerInfo.channel = 0;
  peerInfo.encrypt = false;
  
  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("Fehler beim Hinzufügen des Peers (Wetterstation)!");
    espnowInitialized = false;
    return;
  }
  
  Serial.print("Peer 1 (Wetterstation) hinzugefügt: ");
  for (int i = 0; i < 6; i++) {
    Serial.printf("%02X", weatherStationMAC[i]);
    if (i < 5) Serial.print(":");
  }
  Serial.println();
  
  // Peer 2 (RSSI Monitor) hinzufügen
  memset(&peerInfo, 0, sizeof(peerInfo));
  memcpy(peerInfo.peer_addr, rssiMonitorMAC, 6);
  peerInfo.channel = 0;
  peerInfo.encrypt = false;
  
  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("Warnung: Fehler beim Hinzufügen des RSSI Monitors!");
    // Nicht kritisch - weiter machen
  } else {
    Serial.print("Peer 2 (RSSI Monitor) hinzugefügt: ");
    for (int i = 0; i < 6; i++) {
      Serial.printf("%02X", rssiMonitorMAC[i]);
      if (i < 5) Serial.print(":");
    }
    Serial.println();
  }
  
  espnowInian Wetterstation senden
  esp_err_t result1 = esp_now_send(weatherStationMAC, (uint8_t *)&dataToSend, sizeof(dataToSend));
  
  // Daten an RSSI Monitor senden
  esp_err_t result2 = esp_now_send(rssiMonitorMAC, (uint8_t *)&dataToSend, sizeof(dataToSend));
  
  if (result1 == ESP_OK || result2 == ESP_OK) {
    espnowSendCount++;
    Serial.printf("ESP-NOW: Sende Daten #%d (%.1f cm, ADC: %d, Pumpe: %s, Alarm: %s, RefZeit: %lu s, Letzter Lauf: %lu s)\n", 
                  espnowSendCount, waterLevel, adcValue, pumpActive ? "EIN" : "AUS",
                  pumpAlarmActive ? "JA" : "NEIN", pumpReferenceTime, lastPumpDuration);
    Serial.printf("  → Wetterstation: %s | RSSI Monitor: %s\n", 
                  result1 == ESP_OK ? "OK" : "FEHLER",
                  result2 == ESP_OK ? "OK" : "FEHLER");
  } else {
    Serial.println("ESP-NOW: Fehler beim Senden an beide Peers
  // Datenstruktur füllen
  dataToSend.waterLevel = waterLevel;
  dataToSend.adcValue = adcValue;
  dataToSend.pumpActive = pumpActive;
  dataToSend.pumpAlarm = pumpAlarmActive;
  dataToSend.pumpReferenceTime = pumpReferenceTime;
  dataToSend.lastPumpDuration = lastPumpDuration;
  
  // Daten senden
  esp_err_t result = esp_now_send(weatherStationMAC, (uint8_t *)&dataToSend, sizeof(dataToSend));
  
  if (result == ESP_OK) {
    espnowSendCount++;
    Serial.printf("ESP-NOW: Sende Daten #%d (%.1f cm, ADC: %d, Pumpe: %s, Alarm: %s, RefZeit: %lu s, Letzter Lauf: %lu s)\n", 
                  espnowSendCount, waterLevel, adcValue, pumpActive ? "EIN" : "AUS",
                  pumpAlarmActive ? "JA" : "NEIN", pumpReferenceTime, lastPumpDuration);
  } else {
    Serial.println("ESP-NOW: Fehler beim Senden!");
  }
}

// Funktion zum Zeichnen des Pumpen-Modus-Buttons
void drawPumpModeButton() {
  // Button-Hintergrund je nach Modus
  uint16_t bgColor, textColor;
  const char* buttonText;
  
  switch (pumpMode) {
    case MODE_AUTO:
      bgColor = TFT_BLUE;
      textColor = TFT_WHITE;
      buttonText = "AUTO";
      break;
    case MODE_MANUAL_ON:
      bgColor = TFT_GREEN;
      textColor = TFT_BLACK;
      buttonText = "MANUELL EIN";
      break;
    case MODE_MANUAL_OFF:
      bgColor = TFT_RED;
      textColor = TFT_WHITE;
      buttonText = "MANUELL AUS";
      break;
  }
  
  // Button zeichnen
  tft.fillRoundRect(BUTTON_X, BUTTON_Y, BUTTON_W, BUTTON_H, 8, bgColor);
  tft.drawRoundRect(BUTTON_X, BUTTON_Y, BUTTON_W, BUTTON_H, 8, TFT_WHITE);
  
  // Text zentrieren
  tft.setTextColor(textColor, bgColor);
  tft.setTextSize(2);
  int textWidth = strlen(buttonText) * 12; // Ungefähre Textbreite
  int textX = BUTTON_X + (BUTTON_W - textWidth) / 2;
  int textY = BUTTON_Y + (BUTTON_H - 16) / 2;
  tft.setCursor(textX, textY);
  tft.println(buttonText);
}

// Funktion zum Zeichnen des Verlaufs-Graphen
void drawGraph() {
  // Graph-Hintergrund und Rahmen
  tft.fillRect(GRAPH_X, GRAPH_Y, GRAPH_WIDTH, GRAPH_HEIGHT, TFT_BLACK);
  tft.drawRect(GRAPH_X, GRAPH_Y, GRAPH_WIDTH, GRAPH_HEIGHT, TFT_WHITE);
  
  // Horizontale Gitterlinien (15, 20, 25, 30 cm)
  tft.setTextSize(1);
  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  for (int i = 0; i <= 3; i++) {
    float level = GRAPH_MIN + i * 5.0;
    int y = GRAPH_Y + GRAPH_HEIGHT - (int)((level - GRAPH_MIN) * GRAPH_HEIGHT / (GRAPH_MAX - GRAPH_MIN));
    tft.drawLine(GRAPH_X + 1, y, GRAPH_X + GRAPH_WIDTH - 1, y, TFT_DARKGREY);
    // Y-Achsen-Beschriftung
    tft.setCursor(GRAPH_X + 3, y - 4);
    tft.printf("%.0f", level);
  }
  
  // X-Achsen-Beschriftung (Stunden)
  // Zeitachse von links (24h) nach rechts (0h = jetzt)
  for (int h = 0; h <= 24; h += 4) {
    int x = GRAPH_X + (GRAPH_WIDTH * (24 - h)) / 24;
    tft.drawLine(x, GRAPH_Y + GRAPH_HEIGHT - 1, x, GRAPH_Y + GRAPH_HEIGHT + 3, TFT_DARKGREY);
    tft.setCursor(x - 6, GRAPH_Y + GRAPH_HEIGHT + 5);
    tft.printf("%dh", h);
  }
  
  // Pumpen-Schwellwerte hervorheben
  int pumpOnY = GRAPH_Y + GRAPH_HEIGHT - (int)((pumpOnLevel - GRAPH_MIN) * GRAPH_HEIGHT / (GRAPH_MAX - GRAPH_MIN));
  int pumpOffY = GRAPH_Y + GRAPH_HEIGHT - (int)((pumpOffLevel - GRAPH_MIN) * GRAPH_HEIGHT / (GRAPH_MAX - GRAPH_MIN));
  tft.drawLine(GRAPH_X + 1, pumpOnY, GRAPH_X + GRAPH_WIDTH - 1, pumpOnY, TFT_GREEN);
  tft.drawLine(GRAPH_X + 1, pumpOffY, GRAPH_X + GRAPH_WIDTH - 1, pumpOffY, TFT_RED);
  
  // Datenpunkte zeichnen
  for (int i = 1; i < GRAPH_SAMPLES; i++) {
    int idx1 = (graphIndex + i - 1) % GRAPH_SAMPLES;
    int idx2 = (graphIndex + i) % GRAPH_SAMPLES;
    
    // Nur zeichnen, wenn Werte gültig sind (nicht initial 0)
    if (graphData[idx1] >= GRAPH_MIN || graphData[idx2] >= GRAPH_MIN) {
      // Werte auf Graph-Bereich begrenzen
      float val1 = constrain(graphData[idx1], GRAPH_MIN, GRAPH_MAX);
      float val2 = constrain(graphData[idx2], GRAPH_MIN, GRAPH_MAX);
      
      // Y-Position berechnen (invertiert, da 0 oben ist)
      int y1 = GRAPH_Y + GRAPH_HEIGHT - (int)((val1 - GRAPH_MIN) * GRAPH_HEIGHT / (GRAPH_MAX - GRAPH_MIN));
      int y2 = GRAPH_Y + GRAPH_HEIGHT - (int)((val2 - GRAPH_MIN) * GRAPH_HEIGHT / (GRAPH_MAX - GRAPH_MIN));
      
      // Linie zwischen Punkten zeichnen
      tft.drawLine(GRAPH_X + i - 1, y1, GRAPH_X + i, y2, TFT_CYAN);
    }
  }
  
  // Graph-Titel
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.setTextSize(1);
  tft.setCursor(GRAPH_X + 20, GRAPH_Y - 12);
  tft.println("Verlauf (24h)");
}

// Funktion zum Zeichnen des Seitenwechsel-Buttons
void drawPageButton() {
  uint16_t bgColor = TFT_DARKGREY;
  uint16_t textColor = TFT_WHITE;
  const char* buttonText = (currentPage == PAGE_MAIN) ? "SETUP >" : "< MAIN";
  
  // Unterschiedliche Positionen je nach Seite
  int btnX = (currentPage == PAGE_MAIN) ? PAGE_BTN_MAIN_X : PAGE_BTN_SETUP_X;
  int btnY = (currentPage == PAGE_MAIN) ? PAGE_BTN_MAIN_Y : PAGE_BTN_SETUP_Y;
  
  tft.fillRoundRect(btnX, btnY, PAGE_BTN_W, PAGE_BTN_H, 6, bgColor);
  tft.drawRoundRect(btnX, btnY, PAGE_BTN_W, PAGE_BTN_H, 6, TFT_WHITE);
  
  tft.setTextColor(textColor, bgColor);
  tft.setTextSize(2);
  int textWidth = strlen(buttonText) * 12;
  int textX = btnX + (PAGE_BTN_W - textWidth) / 2;
  int textY = btnY + (PAGE_BTN_H - 16) / 2;
  tft.setCursor(textX, textY);
  tft.println(buttonText);
}

// Funktion zum Zeichnen eines Sliders
void drawSlider(int sliderNum, float value, const char* label) {
  int yPos = (sliderNum == 1) ? SLIDER_Y_ON : SLIDER_Y_OFF;
  
  // Label
  tft.setTextSize(2);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setCursor(10, yPos + 5);
  tft.println(label);
  
  // Slider-Hintergrund
  tft.fillRect(SLIDER_X, yPos, SLIDER_W, SLIDER_H, TFT_DARKGREY);
  tft.drawRect(SLIDER_X, yPos, SLIDER_W, SLIDER_H, TFT_WHITE);
  
  // Slider-Füllung (Wert)
  int fillWidth = (int)((value - SLIDER_MIN) * SLIDER_W / (SLIDER_MAX - SLIDER_MIN));
  fillWidth = constrain(fillWidth, 0, SLIDER_W);
  uint16_t fillColor = (sliderNum == 1) ? TFT_GREEN : TFT_RED;
  tft.fillRect(SLIDER_X + 1, yPos + 1, fillWidth - 1, SLIDER_H - 2, fillColor);
  
  // Wert anzeigen
  tft.setTextSize(2);
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.setCursor(SLIDER_X + SLIDER_W + 10, yPos + 5);
  tft.printf("%.1f cm  ", value);
}

// Seite 1: Hauptansicht zeichnen
void drawMainPage() {
  tft.fillScreen(TFT_BLACK);
  
  // Überschrift
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.setTextSize(2);
  tft.setCursor(10, 10);
  tft.println("Zisternen-Monitor");
  
  // Pumpen-Modus-Button
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(2);
  tft.setCursor(10, 35);
  tft.println("Pumpenmodus:");
  drawPumpModeButton();
  
  // Wasserstand
  tft.setCursor(10, 110);
  tft.println("Wasserstand:");
  
  // Status
  tft.setCursor(10, 195);
  tft.println("Status:");
  
  // Graph zeichnen
  drawGraph();
  
  // Seitenwechsel-Button
  drawPageButton();
}

// Seite 2: Einstellungen zeichnen
void drawSettingsPage() {
  tft.fillScreen(TFT_BLACK);
  
  // Überschrift
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.setTextSize(2);
  tft.setCursor(10, 10);
  tft.println("Einstellungen");
  
  // Slider zeichnen
  drawSlider(1, pumpOnLevel, "EIN:");
  drawSlider(2, pumpOffLevel, "AUS:");
  
  // Parameter-Infos - optimiertes Layout
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(2);
  
  // Luftpumpe oben
  tft.setCursor(10, 200);
  tft.println("Luftpumpe:");
  
  // ESP-NOW darunter
  tft.setCursor(10, 260);
  tft.println("ESP-NOW:");
  
  // Pumpwatch links unten
  tft.setCursor(10, 290);
  tft.println("Pumpwatch:");
  
  // Seitenwechsel-Button
  drawPageButton();
}

// Touch-Kalibrierungsmodus mit visueller Anzeige
void touchCalibrationMode() {
  static bool screenInitialized = false;
  static int lastTouchX = -1, lastTouchY = -1;
  
  // Einmalige Initialisierung des Kalibrierungsbildschirms
  if (!screenInitialized) {
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    tft.setTextSize(2);
    tft.setCursor(10, 10);
    tft.println("TOUCH KALIBRIERUNG");
    
    tft.setTextSize(1);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setCursor(10, 40);
    tft.println("Beruehre den Bildschirm!");
    tft.setCursor(10, 55);
    tft.println("Kreise zeigen Touch-Position");
    
    // Button-Bereich als Rechteck anzeigen
    tft.drawRect(BUTTON_X, BUTTON_Y, BUTTON_W, BUTTON_H, TFT_GREEN);
    tft.setCursor(BUTTON_X + 5, BUTTON_Y + 5);
    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    tft.println("Ziel-Button");
    
    // Display-Raster zeichnen (alle 100 Pixel)
    tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
    for (int x = 0; x <= 480; x += 100) {
      tft.drawLine(x, 0, x, 320, TFT_DARKGREY);
      if (x < 480) {
        tft.setCursor(x + 2, 305);
        tft.printf("%d", x);
      }
    }
    for (int y = 0; y <= 320; y += 100) {
      tft.drawLine(0, y, 480, y, TFT_DARKGREY);
      if (y > 0 && y < 320) {
        tft.setCursor(2, y + 2);
        tft.printf("%d", y);
      }
    }
    
    screenInitialized = true;
  }
  
  // Touch prüfen
  if (touch.touched()) {
    TS_Point p = touch.getPoint();
    
    // Touch-Koordinaten mappen (Achsen invertiert wegen Spiegelung)
    int displayX = map(p.x, 3700, 300, 0, 480);  // Min/Max getauscht = invertiert
    int displayY = map(p.y, 3600, 400, 0, 320);  // Min/Max getauscht = invertiert
    displayX = constrain(displayX, 0, 480);
    displayY = constrain(displayY, 0, 320);
    
    // Alten Touchpoint löschen (schwarzer Kreis)
    if (lastTouchX >= 0 && lastTouchY >= 0) {
      tft.fillCircle(lastTouchX, lastTouchY, 10, TFT_BLACK);
      // Raster wiederherstellen falls übermalt
      if (lastTouchX % 100 < 20 || lastTouchX % 100 > 80) {
        int gridX = (lastTouchX / 100) * 100;
        tft.drawLine(gridX, max(0, lastTouchY-10), gridX, min(320, lastTouchY+10), TFT_DARKGREY);
      }
      if (lastTouchY % 100 < 20 || lastTouchY % 100 > 80) {
        int gridY = (lastTouchY / 100) * 100;
        tft.drawLine(max(0, lastTouchX-10), gridY, min(480, lastTouchX+10), gridY, TFT_DARKGREY);
      }
    }
    
    // Neuen Touchpoint zeichnen (roter Kreis mit weißem Rand)
    tft.fillCircle(displayX, displayY, 8, TFT_RED);
    tft.drawCircle(displayX, displayY, 10, TFT_WHITE);
    
    // Koordinaten anzeigen im Info-Bereich
    tft.fillRect(10, 75, 460, 60, TFT_BLACK);
    tft.setTextSize(1);
    tft.setTextColor(TFT_CYAN, TFT_BLACK);
    tft.setCursor(10, 75);
    tft.printf("RAW:    x=%4d  y=%4d  z=%4d", p.x, p.y, p.z);
    tft.setCursor(10, 90);
    tft.printf("MAPPED: x=%3d   y=%3d", displayX, displayY);
    tft.setCursor(10, 105);
    if (displayX >= BUTTON_X && displayX <= (BUTTON_X + BUTTON_W) &&
        displayY >= BUTTON_Y && displayY <= (BUTTON_Y + BUTTON_H)) {
      tft.setTextColor(TFT_GREEN, TFT_BLACK);
      tft.println(">>> IM BUTTON-BEREICH! <<<");
    } else {
      tft.setTextColor(TFT_RED, TFT_BLACK);
      tft.println("Ausserhalb Button");
    }
    tft.setCursor(10, 120);
    tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    tft.printf("Button: X=%d-%d Y=%d-%d", BUTTON_X, BUTTON_X+BUTTON_W, BUTTON_Y, BUTTON_Y+BUTTON_H);
    
    // Serielle Ausgabe
    Serial.printf("RAW: x=%d, y=%d, z=%d -> MAPPED: x=%d, y=%d", 
                  p.x, p.y, p.z, displayX, displayY);
    if (displayX >= BUTTON_X && displayX <= (BUTTON_X + BUTTON_W) &&
        displayY >= BUTTON_Y && displayY <= (BUTTON_Y + BUTTON_H)) {
      Serial.println(" [IM BUTTON!]");
    } else {
      Serial.println();
    }
    
    lastTouchX = displayX;
    lastTouchY = displayY;
    
    delay(50); // Entprellung
  }
}

// Touch-Handling für Pumpen-Modus-Button, Seitenwechsel und Slider
void checkTouchButton() {
#if TOUCH_ENABLED
  // Prüfen ob Display berührt wurde
  if (touch.touched()) {
    TS_Point p = touch.getPoint();
    
    // Touch-Koordinaten mappen (Achsen invertiert wegen Spiegelung)
    int displayX = map(p.x, 3700, 300, 0, 480);
    int displayY = map(p.y, 3600, 400, 0, 320);
    
    // Begrenzung auf gültige Display-Bereiche
    displayX = constrain(displayX, 0, 480);
    displayY = constrain(displayY, 0, 320);
    
    Serial.printf("Touch: x=%d, y=%d\n", displayX, displayY);
    
    // Seitenwechsel-Button prüfen (unterschiedliche Position je Seite)
    int btnX = (currentPage == PAGE_MAIN) ? PAGE_BTN_MAIN_X : PAGE_BTN_SETUP_X;
    int btnY = (currentPage == PAGE_MAIN) ? PAGE_BTN_MAIN_Y : PAGE_BTN_SETUP_Y;
    Serial.printf("Page-Button: X=%d-%d, Y=%d-%d\n", btnX, btnX+PAGE_BTN_W, btnY, btnY+PAGE_BTN_H);
    if (displayX >= btnX && displayX <= (btnX + PAGE_BTN_W) &&
        displayY >= btnY && displayY <= (btnY + PAGE_BTN_H)) {
      
      Serial.println(">>> SEITENWECHSEL ERKANNT! <<<");
      currentPage = (currentPage == PAGE_MAIN) ? PAGE_SETTINGS : PAGE_MAIN;
      
      // Seite komplett neu zeichnen
      if (currentPage == PAGE_MAIN) {
        drawMainPage();
      } else {
        drawSettingsPage();
      }
      
      // Button nochmal zeichnen damit er nicht überdeckt wird
      drawPageButton();
      
      // Warten bis Touch losgelassen wird
      while (touch.touched()) {
        delay(10);
      }
      delay(100);
      return;
    }
    
    // Hauptseite: Pumpen-Modus-Button prüfen
    if (currentPage == PAGE_MAIN) {
      if (displayX >= BUTTON_X && displayX <= (BUTTON_X + BUTTON_W) &&
          displayY >= BUTTON_Y && displayY <= (BUTTON_Y + BUTTON_H)) {
        
        Serial.println("Pumpenmodus-Button!");
        
        // Modus umschalten
        switch (pumpMode) {
          case MODE_AUTO:
            pumpMode = MODE_MANUAL_ON;
            Serial.println(">>> PUMPE MANUELL EIN <<<");
            break;
          case MODE_MANUAL_ON:
            pumpMode = MODE_MANUAL_OFF;
            Serial.println(">>> PUMPE MANUELL AUS <<<");
            break;
          case MODE_MANUAL_OFF:
            pumpMode = MODE_AUTO;
            Serial.println(">>> PUMPE AUTO-MODUS <<<");
            break;
        }
        
        // Button neu zeichnen
        drawPumpModeButton();
        
        // Warten bis Touch losgelassen wird
        while (touch.touched()) {
          delay(10);
        }
        delay(100);
      }
    }
    
    // Einstellungsseite: Slider prüfen
    if (currentPage == PAGE_SETTINGS) {
      // Slider 1 (EIN-Level)
      if (displayX >= SLIDER_X && displayX <= (SLIDER_X + SLIDER_W) &&
          displayY >= SLIDER_Y_ON && displayY <= (SLIDER_Y_ON + SLIDER_H)) {
        
        sliderDragging = true;
        draggedSlider = 1;
        
        // Wert aus Position berechnen
        int sliderPos = displayX - SLIDER_X;
        pumpOnLevel = SLIDER_MIN + (sliderPos * (SLIDER_MAX - SLIDER_MIN) / SLIDER_W);
        pumpOnLevel = constrain(pumpOnLevel, SLIDER_MIN, SLIDER_MAX);
        
        // Sicherstellen dass EIN-Level über AUS-Level liegt
        if (pumpOnLevel <= pumpOffLevel) {
          pumpOnLevel = pumpOffLevel + 1.0;
        }
        
        drawSlider(1, pumpOnLevel, "EIN:");
        Serial.printf("Slider EIN: %.1f cm\n", pumpOnLevel);
      }
      
      // Slider 2 (AUS-Level)
      if (displayX >= SLIDER_X && displayX <= (SLIDER_X + SLIDER_W) &&
          displayY >= SLIDER_Y_OFF && displayY <= (SLIDER_Y_OFF + SLIDER_H)) {
        
        sliderDragging = true;
        draggedSlider = 2;
        
        // Wert aus Position berechnen
        int sliderPos = displayX - SLIDER_X;
        pumpOffLevel = SLIDER_MIN + (sliderPos * (SLIDER_MAX - SLIDER_MIN) / SLIDER_W);
        pumpOffLevel = constrain(pumpOffLevel, SLIDER_MIN, SLIDER_MAX);
        
        // Sicherstellen dass AUS-Level unter EIN-Level liegt
        if (pumpOffLevel >= pumpOnLevel) {
          pumpOffLevel = pumpOnLevel - 1.0;
        }
        
        drawSlider(2, pumpOffLevel, "AUS:");
        Serial.printf("Slider AUS: %.1f cm\n", pumpOffLevel);
      }
      
      // Während Dragging: Position kontinuierlich aktualisieren
      while (touch.touched() && sliderDragging) {
        TS_Point p2 = touch.getPoint();
        int newX = map(p2.x, 3700, 300, 0, 480);
        newX = constrain(newX, 0, 480);
        
        if (newX >= SLIDER_X && newX <= (SLIDER_X + SLIDER_W)) {
          int sliderPos = newX - SLIDER_X;
          float newValue = SLIDER_MIN + (sliderPos * (SLIDER_MAX - SLIDER_MIN) / SLIDER_W);
          newValue = constrain(newValue, SLIDER_MIN, SLIDER_MAX);
          
          if (draggedSlider == 1) {
            if (newValue > pumpOffLevel) {
              pumpOnLevel = newValue;
              drawSlider(1, pumpOnLevel, "EIN:");
            }
          } else if (draggedSlider == 2) {
            if (newValue < pumpOnLevel) {
              pumpOffLevel = newValue;
              drawSlider(2, pumpOffLevel, "AUS:");
            }
          }
        }
        delay(50);
      }
      
      // Wenn Slider losgelassen: Werte speichern
      if (sliderDragging) {
        sliderDragging = false;
        draggedSlider = 0;
        
        // Werte in Preferences speichern
        preferences.begin("pumpSettings", false);
        preferences.putFloat("onLevel", pumpOnLevel);
        preferences.putFloat("offLevel", pumpOffLevel);
        preferences.end();
        
        Serial.printf("Schwellwerte gespeichert: EIN=%.1f cm, AUS=%.1f cm\n", 
                     pumpOnLevel, pumpOffLevel);
      }
    }
  }
#else
  // Serielle Steuerung als Alternative
  if (Serial.available()) {
    char cmd = Serial.read();
    if (cmd == 'm' || cmd == 'M') {
      // Modus umschalten
      switch (pumpMode) {
        case MODE_AUTO:
          pumpMode = MODE_MANUAL_ON;
          Serial.println(">>> PUMPE MANUELL EIN <<<");
          break;
        case MODE_MANUAL_ON:
          pumpMode = MODE_MANUAL_OFF;
          Serial.println(">>> PUMPE MANUELL AUS <<<");
          break;
        case MODE_MANUAL_OFF:
          pumpMode = MODE_AUTO;
          Serial.println(">>> PUMPE AUTO-MODUS <<<");
          break;
      }
      drawPumpModeButton();
    }
  }
#endif
}

void setup() {
  Serial.begin(115200);
  Serial.println("Zisternen-Monitor gestartet...");
  
  // ADC-Pin konfigurieren
  pinMode(PRESSURE_SENSOR_PIN, INPUT);
  analogReadResolution(12); // 12-Bit ADC (0-4095)
  analogSetAttenuation(ADC_11db); // 0-3,3V Messbereich
  
  // Pumpen-MOSFET konfigurieren
  pinMode(PUMP_MOSFET_PIN, OUTPUT);
  digitalWrite(PUMP_MOSFET_PIN, LOW); // Pumpe initial AUS
  Serial.printf("Pumpen-MOSFET konfiguriert auf GPIO %d (OUTPUT, initial LOW)\n", PUMP_MOSFET_PIN);
  
  // Luftpumpen-MOSFET konfigurieren
  pinMode(AIR_PUMP_PIN, OUTPUT);
  digitalWrite(AIR_PUMP_PIN, LOW); // Luftpumpe initial AUS
  Serial.printf("Luftpumpen-MOSFET konfiguriert auf GPIO %d (OUTPUT, initial LOW)\n", AIR_PUMP_PIN);
  
  // ESP-NOW initialisieren
  initESPNow();
  
  // Pumpüberwachung: Gespeicherte Referenzzeit laden
  preferences.begin("pumpMonitor", true); // readonly
  pumpReferenceTime = preferences.getULong("refTime", 0);
  pumpRunCount = preferences.getInt("runCount", 0);
  preferences.end();
  
  if (pumpReferenceTime > 0) {
    Serial.printf("Gespeicherte Referenzzeit geladen: %lu Sekunden\n", pumpReferenceTime);
    Serial.printf("Anzahl Messungen: %d\n", pumpRunCount);
  } else {
    Serial.println("Keine Referenzzeit gespeichert - Lernphase startet (3 Messungen benötigt)");
  };
  
  // Pumpen-Schwellwerte laden
  preferences.begin("pumpSettings", true); // readonly
  pumpOnLevel = preferences.getFloat("onLevel", 30.0);
  pumpOffLevel = preferences.getFloat("offLevel", 15.0);
  preferences.end();
  Serial.printf("Schwellwerte geladen: EIN=%.1f cm, AUS=%.1f cm\n", pumpOnLevel, pumpOffLevel);
  
  // SPI Bus mit custom Pins initialisieren (MUSS vor Display UND Touch erfolgen!)
  // Diese Pins müssen mit platformio.ini übereinstimmen
  SPI.begin(14, 12, 13, -1); // SCK=14, MISO=12, MOSI=13, SS=-1 (nicht verwendet)
  Serial.println("SPI initialisiert: SCK=14, MISO=12, MOSI=13");
  
  // Display initialisieren (verwendet bereits initialisierten SPI-Bus)
  tft.init();
  tft.setRotation(1); // Landscape-Modus (0-3 möglich)
  Serial.println("TFT Display initialisiert");
  
  // Touch-Controller initialisieren (verwendet bereits initialisierten SPI-Bus)
  #if TOUCH_ENABLED
  touch.begin(); // Verwendet jetzt den bereits initialisierten SPI-Bus
  touch.setRotation(1); // Gleiche Rotation wie Display
  Serial.println("XPT2046 Touch initialisiert");
  Serial.printf("Touch: CS=%d, IRQ=%d\\n", TOUCH_CS, TOUCH_IRQ);
  Serial.println("Tippen Sie auf den Button, um den Modus zu wechseln");
  #else
  Serial.println("Touch deaktiviert - Sende 'M' über Serial zum Umschalten");
  #endif
  
  // Backlight einschalten
  pinMode(27, OUTPUT);
  digitalWrite(27, HIGH);
  
  // Hintergrund schwarz
  tft.fillScreen(TFT_BLACK);
  
  // Ringpuffer initialisieren
  for (int i = 0; i < GRAPH_SAMPLES; i++) {
    graphData[i] = 0.0;
  }
  
#if TOUCH_CALIBRATION_MODE
  // Kalibrierungsmodus - minimale Anzeige
  Serial.println("*** TOUCH KALIBRIERUNGSMODUS AKTIV ***");
  Serial.println("*** Setze TOUCH_CALIBRATION_MODE auf false nach Kalibrierung ***");
#else
  // Hauptseite initial zeichnen
  drawMainPage();
#endif
  
  Serial.println("Display initialisiert!");
}

void loop() {
  unsigned long currentTime = millis();
  
#if TOUCH_CALIBRATION_MODE
  // Touch-Kalibrierungsmodus - nur Touch-Visualisierung
  touchCalibrationMode();
  delay(10);
  return; // Rest des Loops überspringen
#endif

  // Alarm-Blink-Logik (roter TFT-Hintergrund)
  if (pumpAlarmActive && (currentTime - lastAlarmBlink >= ALARM_BLINK_INTERVAL)) {
    lastAlarmBlink = currentTime;
    pumpAlarmBlink = !pumpAlarmBlink;
    
    // Hintergrund rot blinken lassen (nur beim Umschalten auf rot)
    if (pumpAlarmBlink) {
      tft.fillScreen(TFT_RED);
      // Overlay-Text für Alarm
      tft.setTextSize(3);
      tft.setTextColor(TFT_WHITE, TFT_RED);
      int textX = (480 - 15 * 12) / 2; // Zentrieren
      tft.setCursor(textX, 150);
      tft.println("PUMPEN-ALARM!");
    } else {
      // Zurück zur aktuellen Seite
      if (currentPage == PAGE_MAIN) {
        drawMainPage();
      } else {
        drawSettingsPage();
      }
    }
  }
  
  // Touch-Button überprüfen
  checkTouchButton();
  
  // Luftpumpen-Steuerung (alle 5 Minuten für 10 Sekunden)
  if (!airPumpActive && (currentTime - lastAirPumpTime >= AIR_PUMP_INTERVAL)) {
    // Luftpumpe einschalten
    airPumpActive = true;
    airPumpStartTime = currentTime;
    lastAirPumpTime = currentTime;
    digitalWrite(AIR_PUMP_PIN, HIGH);
    Serial.println(">>> LUFTPUMPE GESTARTET (Druckausgleich) <<<");
  }
  
  // Luftpumpe nach 10 Sekunden ausschalten
  if (airPumpActive && (currentTime - airPumpStartTime >= AIR_PUMP_DURATION)) {
    airPumpActive = false;
    digitalWrite(AIR_PUMP_PIN, LOW);
    Serial.println(">>> LUFTPUMPE GESTOPPT <<<");
  }
  
  // Messung überspringen, wenn Luftpumpe aktiv ist
  if (airPumpActive) {
    delay(500);
    return; // Loop beenden und neu starten
  }
  
  // ADC-Wert vom Drucksensor lesen (Mittelwert aus 100 Messungen für stabilere Werte)
  int adcSum = 0;
  for (int i = 0; i < 100; i++) {
    adcSum += analogRead(PRESSURE_SENSOR_PIN);
    delay(10);
  }
  int adcValue = adcSum / 100;
  
  // Wasserstand in cm berechnen (lineare Interpolation)
  // Formel: wasserstand_cm = (adcValue - ADC_MIN) * WATER_LEVEL_MAX / (ADC_MAX - ADC_MIN)
  float waterLevelCm = (float)(adcValue - ADC_MIN) * WATER_LEVEL_MAX / (ADC_MAX - ADC_MIN);
  
  // Negative Werte auf 0 begrenzen
  if (waterLevelCm < 0) waterLevelCm = 0;
  if (waterLevelCm > WATER_LEVEL_MAX) waterLevelCm = WATER_LEVEL_MAX;
  
  // Pumpensteuerung abhängig vom Modus
  switch (pumpMode) {
    case MODE_AUTO:
      // Automatische Steuerung mit Hysterese
      if (waterLevelCm >= pumpOnLevel && !pumpActive) {
        // Pumpe einschalten bei >= pumpOnLevel cm
        pumpActive = true;
        pumpStartTime = millis(); // Timer starten
        digitalWrite(PUMP_MOSFET_PIN, HIGH);
        Serial.println(">>> PUMPE EINGESCHALTET (AUTO) <<<");
        Serial.printf(">>> GPIO %d auf HIGH gesetzt (Wasserstand: %.1f cm) <<<\n", PUMP_MOSFET_PIN, waterLevelCm);
        Serial.printf(">>> Timer gestartet: %lu ms <<<\n", pumpStartTime);
        int pinState = digitalRead(PUMP_MOSFET_PIN);
        Serial.printf(">>> GPIO %d Status: %d (sollte 1 sein) <<<\n", PUMP_MOSFET_PIN, pinState);
      }
      else if (waterLevelCm <= pumpOffLevel && pumpActive) {
        // Pumpe ausschalten bei <= pumpOffLevel cm
        pumpActive = false;
        digitalWrite(PUMP_MOSFET_PIN, LOW);
        
        // Laufzeit berechnen (nur im AUTO-Modus für Überwachung)
        unsigned long pumpRunTime = (millis() - pumpStartTime) / 1000; // in Sekunden
        lastPumpDuration = pumpRunTime; // Letzte Laufzeit speichern für ESP-NOW
        Serial.println(">>> PUMPE AUSGESCHALTET (AUTO) <<<");
        Serial.printf(">>> GPIO %d auf LOW gesetzt (Wasserstand: %.1f cm) <<<\n", PUMP_MOSFET_PIN, waterLevelCm);
        Serial.printf(">>> Laufzeit: %lu Sekunden <<<\n", pumpRunTime);
        
        // Lernphase: Erste 3 Messungen speichern
        if (pumpRunCount < 3) {
          pumpRunTimes[pumpRunCount] = pumpRunTime;
          pumpRunCount++;
          Serial.printf(">>> Lernphase: Messung %d/3 gespeichert <<<\n", pumpRunCount);
          
          // Nach 3 Messungen: Mittelwert berechnen und speichern
          if (pumpRunCount == 3) {
            pumpReferenceTime = (pumpRunTimes[0] + pumpRunTimes[1] + pumpRunTimes[2]) / 3;
            Serial.printf(">>> Referenzzeit berechnet: %lu Sekunden (Mittelwert aus %lu, %lu, %lu) <<<\n", 
                         pumpReferenceTime, pumpRunTimes[0], pumpRunTimes[1], pumpRunTimes[2]);
            
            // Dauerhaft speichern
            preferences.begin("pumpMonitor", false);
            preferences.putULong("refTime", pumpReferenceTime);
            preferences.putInt("runCount", pumpRunCount);
            preferences.end();
            Serial.println(">>> Referenzzeit dauerhaft gespeichert <<<");
          }
        }
        // Überwachungsphase: Überprüfen ob Referenzzeit überschritten
        else if (pumpReferenceTime > 0) {
          unsigned long maxAllowedTime = (unsigned long)(pumpReferenceTime * PUMP_ALARM_THRESHOLD);
          Serial.printf(">>> Überwachung: Laufzeit %lu s vs. Max erlaubt %lu s (%.0f%% von %lu s) <<<\n", 
                       pumpRunTime, maxAllowedTime, PUMP_ALARM_THRESHOLD * 100, pumpReferenceTime);
          
          if (pumpRunTime > maxAllowedTime) {
            // Alarm aktivieren
            bool wasAlarmActive = pumpAlarmActive; // Vorheriger Status
            pumpAlarmActive = true;
            Serial.println("!!! ALARM: Pumpenlaufzeit überschritten !!!");
            Serial.printf("!!! %lu s > %lu s (%.0f%% von Referenz) !!!\n", 
                         pumpRunTime, maxAllowedTime, PUMP_ALARM_THRESHOLD * 100);
            
            // Sofort ESP-NOW Alarm senden (nur beim ersten Mal)
            if (!wasAlarmActive && espnowInitialized) {
              sendWaterLevelData(waterLevelCm, adcValue, pumpActive);
              Serial.println(">>> ESP-NOW Alarm-Meldung sofort gesendet <<<");
            }
          } else {
            pumpAlarmActive = false;
          }
        }
      }
      break;
      
    case MODE_MANUAL_ON:
      // Manuell EIN: Pumpe läuft dauerhaft (ignoriert Wasserstand)
      if (!pumpActive) {
        pumpActive = true;
        pumpStartTime = millis(); // Timer auch im Manuell-Modus starten (für Info)
        digitalWrite(PUMP_MOSFET_PIN, HIGH);
        Serial.println(">>> PUMPE EINGESCHALTET (MANUELL) <<<");
        Serial.printf(">>> GPIO %d auf HIGH gesetzt <<<\n", PUMP_MOSFET_PIN);
        // Pin-Status überprüfen
        int pinState = digitalRead(PUMP_MOSFET_PIN);
        Serial.printf(">>> GPIO %d Status: %d (sollte 1 sein) <<<\n", PUMP_MOSFET_PIN, pinState);
      }
      // Im Manuell-Modus läuft die Pumpe weiter bis Button gedrückt wird
      break;
      
    case MODE_MANUAL_OFF:
      // Manuell AUS: Pumpe bleibt aus (ignoriert Wasserstand)
      if (pumpActive) {
        pumpActive = false;
        digitalWrite(PUMP_MOSFET_PIN, LOW);
        Serial.println(">>> PUMPE AUSGESCHALTET (MANUELL) <<<");
        Serial.printf(">>> GPIO %d auf LOW gesetzt <<<\n", PUMP_MOSFET_PIN);
      }
      break;
  }
  
  // RRD-Daten aktualisieren (alle 5 Sekunden)
  if (currentTime - lastSampleTime >= SAMPLE_INTERVAL) {
    lastSampleTime = currentTime;
    
    // Neuen Wert in Ringpuffer speichern
    graphData[graphIndex] = waterLevelCm;
    graphIndex = (graphIndex + 1) % GRAPH_SAMPLES;
    
    // Graph neu zeichnen (nur auf der Hauptseite)
    if (currentPage == PAGE_MAIN) {
      drawGraph();
    }
  }
  
  // ESP-NOW Daten senden (alle 15 Minuten)
  if (espnowInitialized && (currentTime - lastESPNowSend >= ESPNOW_SEND_INTERVAL)) {
    lastESPNowSend = currentTime;
    sendWaterLevelData(waterLevelCm, adcValue, pumpActive);
  }
  
  // Display-Updates nur alle 3 Sekunden (reduziert Flackern, stabilere Anzeige)
  if (currentTime - lastDisplayUpdate >= DISPLAY_UPDATE_INTERVAL) {
    lastDisplayUpdate = currentTime;
  
    // HAUPTSEITE: Wasserstand, Status, Fortschrittsbalken
    if (currentPage == PAGE_MAIN) {
      // Wasserstand anzeigen
      tft.setTextColor(TFT_GREEN, TFT_BLACK);
      tft.setTextSize(3);
      tft.fillRect(10, 135, 230, 30, TFT_BLACK);
      tft.setCursor(10, 135);
      tft.printf("%.1f cm", waterLevelCm);
      
      // Fortschrittsbalken (kompakt, Maximum 50 cm)
      int barWidth = (int)(waterLevelCm * 200 / 50.0);
      barWidth = constrain(barWidth, 0, 200);
      tft.fillRect(10, 170, 210, 20, TFT_BLACK);
      tft.drawRect(10, 170, 210, 20, TFT_WHITE);
      tft.fillRect(11, 171, barWidth, 18, TFT_BLUE);
      
      // Pumpenstatus anzeigen
      tft.setTextSize(2);
      tft.fillRect(90, 195, 150, 25, TFT_BLACK);
      tft.setCursor(90, 195);
      if (pumpActive) {
        tft.setTextColor(TFT_GREEN, TFT_BLACK);
        tft.println("EIN");
      } else {
        tft.setTextColor(TFT_RED, TFT_BLACK);
        tft.println("AUS");
      }
    }
    
    // EINSTELLUNGSSEITE: Luftpumpe, ESP-NOW, Pumpwatch
    else if (currentPage == PAGE_SETTINGS) {
      // Luftpumpen-Status (bei Y=200)
      tft.setTextSize(2);
      tft.fillRect(150, 200, 110, 25, TFT_BLACK);
      tft.setCursor(150, 200);
      if (airPumpActive) {
        tft.setTextColor(TFT_ORANGE, TFT_BLACK);
        tft.println("AKTIV");
      } else {
        tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
        tft.println("AUS");
      }
      
      // Nächste Luftpumpen-Aktivierung (größere Schrift)
      unsigned long nextAirPump = AIR_PUMP_INTERVAL - (currentTime - lastAirPumpTime);
      int minAir = nextAirPump / 60000;
      int secAir = (nextAirPump % 60000) / 1000;
      tft.setTextSize(2);
      tft.fillRect(270, 200, 150, 25, TFT_BLACK);
      tft.setCursor(270, 205);
      tft.setTextColor(TFT_CYAN, TFT_BLACK);
      tft.printf("%dm %ds", minAir, secAir);
      
      // ESP-NOW Status (bei Y=260, verschoben nach unten)
      tft.setTextSize(2);
      tft.fillRect(120, 260, 110, 25, TFT_BLACK);
      tft.setCursor(120, 260);
      if (espnowInitialized) {
        if (lastSendSuccess) {
          tft.setTextColor(TFT_GREEN, TFT_BLACK);
          tft.println("OK");
        } else {
          tft.setTextColor(TFT_YELLOW, TFT_BLACK);
          tft.println("INIT");
        }
      } else {
        tft.setTextColor(TFT_RED, TFT_BLACK);
        tft.println("FEHLER");
      }
      
      // Nächste ESP-NOW Übertragung (größere Schrift, ausgerichtet mit Luftpumpe bei X=270)
      unsigned long nextESPNow = ESPNOW_SEND_INTERVAL - (currentTime - lastESPNowSend);
      int minESP = nextESPNow / 60000;
      int secESP = (nextESPNow % 60000) / 1000;
      tft.setTextSize(2);
      tft.fillRect(270, 260, 140, 25, TFT_BLACK);
      tft.setCursor(270, 265);
      tft.setTextColor(TFT_CYAN, TFT_BLACK);
      tft.printf("%dm %ds", minESP, secESP);
      
      // Pumpwatch-Status (unten links bei Y=290)
      tft.setTextSize(2);
      tft.fillRect(180, 290, 110, 25, TFT_BLACK);
      tft.setCursor(180, 290);
      if (pumpRunCount < 3) {
        tft.setTextColor(TFT_YELLOW, TFT_BLACK);
        tft.printf("LERN %d/3", pumpRunCount);
      } else if (pumpAlarmActive) {
        tft.setTextColor(TFT_RED, TFT_BLACK);
        tft.println("ALARM!");
      } else {
        tft.setTextColor(TFT_GREEN, TFT_BLACK);
        tft.println("OK");
      }
      
      // Laufzeit / Referenzzeit (unter Pumpwatch)
      tft.setTextSize(1);
      tft.fillRect(300, 295, 150, 15, TFT_BLACK);
      tft.setCursor(300, 295);
      tft.setTextColor(TFT_CYAN, TFT_BLACK);
      if (pumpActive) {
        unsigned long currentRunTime = (millis() - pumpStartTime) / 1000;
        int minutes = currentRunTime / 60;
        int seconds = currentRunTime % 60;
        tft.printf("%dm %ds", minutes, seconds);
      } else if (pumpReferenceTime > 0) {
        int refMinutes = pumpReferenceTime / 60;
        int refSeconds = pumpReferenceTime % 60;
        tft.printf("Ref: %dm %ds", refMinutes, refSeconds);
      } else {
        tft.print("--");
      }
      
      // Schwellwerte-Info (Auto-Modus)
      tft.setTextSize(1);
      tft.fillRect(10, 55, 450, 15, TFT_BLACK);
      tft.setCursor(10, 55);
      tft.setTextColor(TFT_YELLOW, TFT_BLACK);
      if (pumpMode == MODE_AUTO) {
        tft.printf("Auto: EIN %.1fcm / AUS %.1fcm", pumpOnLevel, pumpOffLevel);
      } else if (pumpMode == MODE_MANUAL_ON) {
        tft.printf("Manuell EIN - dauerhaft");
      } else {
        tft.printf("Manuell AUS - keine Pumpe");
      }
      
      // Button nochmal zeichnen damit er nicht von fillRect überdeckt wird
      drawPageButton();
    }
  } // Ende Display-Update-Block
  
  // Serielle Ausgabe
  Serial.printf("ADC: %d | Wasserstand: %.1f cm", adcValue, waterLevelCm);
  Serial.printf(" | Pumpe: %s\n", pumpActive ? "EIN" : "AUS");
  
  delay(100); // Kurze Pause zwischen Messzyklen
}
