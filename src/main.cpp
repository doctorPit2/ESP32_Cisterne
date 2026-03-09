#include <Arduino.h>
#include <TFT_eSPI.h>
#include <esp_now.h>
#include <WiFi.h>

// TFT Display initialisieren
TFT_eSPI tft = TFT_eSPI();

// MAC-Adresse der Wetterstation (muss angepasst werden!)
// Format: {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF}
uint8_t weatherStationMAC[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// Datenstruktur für ESP-NOW Übertragung
typedef struct {
  float waterLevel;      // Wasserstand in cm
  int adcValue;          // Roher ADC-Wert
  bool pumpActive;       // Status der Wasserpumpe
  unsigned long uptime;  // Betriebszeit in Sekunden
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
#define ESPNOW_SEND_INTERVAL 900000  // 15 Minuten in ms (900.000 ms)

// Kalibrierungswerte (anpassen nach Bedarf)
#define ADC_MIN 0          // ADC-Wert bei 0 cm Wasserstand
#define ADC_MAX 4095       // ADC-Wert bei maximalem Wasserstand
#define WATER_LEVEL_MAX 300 // Maximale Füllhöhe in cm (3 Meter)

// Pumpensteuerung (Hysterese)
#define PUMP_ON_LEVEL 30.0   // Pumpe EINschalten bei 30 cm
#define PUMP_OFF_LEVEL 15.0  // Pumpe AUSschalten bei 15 cm

// RRD Graph-Einstellungen
#define GRAPH_WIDTH 220      // Breite des Graphen in Pixel
#define GRAPH_HEIGHT 250     // Höhe des Graphen in Pixel
#define GRAPH_X 250          // X-Position des Graphen
#define GRAPH_Y 50           // Y-Position des Graphen
#define GRAPH_MIN 15.0       // Minimaler Wasserstand im Graph (cm)
#define GRAPH_MAX 30.0       // Maximaler Wasserstand im Graph (cm)
#define GRAPH_SAMPLES 220    // Anzahl der Datenpunkte (= Breite)
#define SAMPLE_INTERVAL 5000 // Messintervall in ms (5 Sekunden)

// Globale Variablen
bool pumpActive = false;
float graphData[GRAPH_SAMPLES]; // Ringpuffer für Messwerte
int graphIndex = 0;              // Aktueller Index im Ringpuffer
unsigned long lastSampleTime = 0; // Zeitpunkt der letzten Messung

// Luftpumpen-Variablen
unsigned long lastAirPumpTime = 0;  // Letzter Start der Luftpumpe
bool airPumpActive = false;          // Status der Luftpumpe
unsigned long airPumpStartTime = 0;  // Startzeit der aktuellen Luftpumpen-Phase

// ESP-NOW Variablen
unsigned long lastESPNowSend = 0;    // Zeitpunkt der letzten ESP-NOW Übertragung
bool espnowInitialized = false;      // ESP-NOW Initialisierungsstatus
int espnowSendCount = 0;             // Anzahl gesendeter Nachrichten
bool lastSendSuccess = false;        // Status der letzten Übertragung

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
    Serial.println("Fehler beim Hinzufügen des Peers!");
    espnowInitialized = false;
    return;
  }
  
  Serial.print("Peer (Wetterstation) hinzugefügt: ");
  for (int i = 0; i < 6; i++) {
    Serial.printf("%02X", weatherStationMAC[i]);
    if (i < 5) Serial.print(":");
  }
  Serial.println();
  
  espnowInitialized = true;
}

// Daten via ESP-NOW senden
void sendWaterLevelData(float waterLevel, int adcValue, bool pumpActive) {
  if (!espnowInitialized) {
    Serial.println("ESP-NOW nicht initialisiert!");
    return;
  }
  
  // Datenstruktur füllen
  dataToSend.waterLevel = waterLevel;
  dataToSend.adcValue = adcValue;
  dataToSend.pumpActive = pumpActive;
  dataToSend.uptime = millis() / 1000;
  
  // Daten senden
  esp_err_t result = esp_now_send(weatherStationMAC, (uint8_t *)&dataToSend, sizeof(dataToSend));
  
  if (result == ESP_OK) {
    espnowSendCount++;
    Serial.printf("ESP-NOW: Sende Daten #%d (%.1f cm, ADC: %d, Pumpe: %s)\n", 
                  espnowSendCount, waterLevel, adcValue, pumpActive ? "EIN" : "AUS");
  } else {
    Serial.println("ESP-NOW: Fehler beim Senden!");
  }
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
  
  // Pumpen-Schwellwerte hervorheben
  int pumpOnY = GRAPH_Y + GRAPH_HEIGHT - (int)((PUMP_ON_LEVEL - GRAPH_MIN) * GRAPH_HEIGHT / (GRAPH_MAX - GRAPH_MIN));
  int pumpOffY = GRAPH_Y + GRAPH_HEIGHT - (int)((PUMP_OFF_LEVEL - GRAPH_MIN) * GRAPH_HEIGHT / (GRAPH_MAX - GRAPH_MIN));
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
  tft.println("Verlauf (5s / Punkt)");
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
  
  // Luftpumpen-MOSFET konfigurieren
  pinMode(AIR_PUMP_PIN, OUTPUT);
  digitalWrite(AIR_PUMP_PIN, LOW); // Luftpumpe initial AUS
  
  // ESP-NOW initialisieren
  initESPNow();
  
  // Display initialisieren
  tft.init();
  tft.setRotation(1); // Landscape-Modus (0-3 möglich)
  
  // Backlight einschalten
  pinMode(27, OUTPUT);
  digitalWrite(27, HIGH);
  
  // Hintergrund schwarz
  tft.fillScreen(TFT_BLACK);
  
  // Ringpuffer initialisieren
  for (int i = 0; i < GRAPH_SAMPLES; i++) {
    graphData[i] = 0.0;
  }
  
  // Überschrift
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.setTextSize(2);
  tft.setCursor(10, 10);
  tft.println("Zisternen-Monitor");
  
  // Statische Beschriftungen (linke Seite)
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(2);
  tft.setCursor(10, 60);
  tft.println("ADC-Wert:");
  
  tft.setCursor(10, 120);
  tft.println("Wasserstand:");
  
  tft.setCursor(10, 210);
  tft.println("Pumpe:");
  
  tft.setCursor(10, 240);
  tft.println("Luftpumpe:");
  
  tft.setCursor(10, 270);
  tft.println("ESP-NOW:");
  
  // Initialen Graph zeichnen
  drawGraph();
  
  Serial.println("Display initialisiert!");
}

void loop() {
  unsigned long currentTime = millis();
  
  // Luftpumpen-Steuerung (alle 5 Minuten für 10 Sekunden)
  if (!airPumpActive && (currentTime - lastAirPumpTime >= AIR_PUMP_INTERVAL)) {
    // Luftpumpe einschalten
    airPumpActive = true;
    airPumpStartTime = currentTime;
    lastAirPumpTime = currentTime;
    digitalWrite(AIR_PUMP_PIN, HIGH);
    Serial.println(">>> LUFTPUMPE GESTARTET (Druckausgleich) <<<");
    
    // Status auf Display aktualisieren
    tft.setTextSize(2);
    tft.fillRect(130, 240, 110, 25, TFT_BLACK);
    tft.setCursor(130, 240);
    tft.setTextColor(TFT_ORANGE, TFT_BLACK);
    tft.println("AKTIV");
  }
  
  // Luftpumpe nach 10 Sekunden ausschalten
  if (airPumpActive && (currentTime - airPumpStartTime >= AIR_PUMP_DURATION)) {
    airPumpActive = false;
    digitalWrite(AIR_PUMP_PIN, LOW);
    Serial.println(">>> LUFTPUMPE GESTOPPT <<<");
    
    // Status auf Display aktualisieren
    tft.setTextSize(2);
    tft.fillRect(130, 240, 110, 25, TFT_BLACK);
    tft.setCursor(130, 240);
    tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
    tft.println("AUS");
  }
  
  // Messung überspringen, wenn Luftpumpe aktiv ist
  if (airPumpActive) {
    delay(500);
    return; // Loop beenden und neu starten
  }
  
  // ADC-Wert vom Drucksensor lesen (Mittelwert aus 10 Messungen)
  int adcSum = 0;
  for (int i = 0; i < 10; i++) {
    adcSum += analogRead(PRESSURE_SENSOR_PIN);
    delay(10);
  }
  int adcValue = adcSum / 10;
  
  // Wasserstand in cm berechnen (lineare Interpolation)
  // Formel: wasserstand_cm = (adcValue - ADC_MIN) * WATER_LEVEL_MAX / (ADC_MAX - ADC_MIN)
  float waterLevelCm = (float)(adcValue - ADC_MIN) * WATER_LEVEL_MAX / (ADC_MAX - ADC_MIN);
  
  // Negative Werte auf 0 begrenzen
  if (waterLevelCm < 0) waterLevelCm = 0;
  if (waterLevelCm > WATER_LEVEL_MAX) waterLevelCm = WATER_LEVEL_MAX;
  
  // Pumpensteuerung mit Hysterese
  if (waterLevelCm >= PUMP_ON_LEVEL && !pumpActive) {
    // Pumpe einschalten bei >= 30 cm
    pumpActive = true;
    digitalWrite(PUMP_MOSFET_PIN, HIGH);
    Serial.println(">>> PUMPE EINGESCHALTET <<<");
  }
  else if (waterLevelCm <= PUMP_OFF_LEVEL && pumpActive) {
    // Pumpe ausschalten bei <= 15 cm
    pumpActive = false;
    digitalWrite(PUMP_MOSFET_PIN, LOW);
    Serial.println(">>> PUMPE AUSGESCHALTET <<<");
  }
  
  // RRD-Daten aktualisieren (alle 5 Sekunden)
  if (currentTime - lastSampleTime >= SAMPLE_INTERVAL) {
    lastSampleTime = currentTime;
    
    // Neuen Wert in Ringpuffer speichern
    graphData[graphIndex] = waterLevelCm;
    graphIndex = (graphIndex + 1) % GRAPH_SAMPLES;
    
    // Graph neu zeichnen
    drawGraph();
  }
  
  // ESP-NOW Daten senden (alle 15 Minuten)
  if (espnowInitialized && (currentTime - lastESPNowSend >= ESPNOW_SEND_INTERVAL)) {
    lastESPNowSend = currentTime;
    sendWaterLevelData(waterLevelCm, adcValue, pumpActive);
  }
  
  // ADC-Wert anzeigen (linke Seite, kompakt)
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.setTextSize(2);
  tft.fillRect(10, 85, 230, 20, TFT_BLACK); // Bereich löschen
  tft.setCursor(10, 85);
  tft.printf("%4d / 4095", adcValue);
  
  // Wasserstand anzeigen
  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  tft.setTextSize(3);
  tft.fillRect(10, 145, 230, 30, TFT_BLACK); // Bereich löschen
  tft.setCursor(10, 145);
  tft.printf("%.1f cm", waterLevelCm);
  
  // Fortschrittsbalken (kompakt)
  int barWidth = (int)(waterLevelCm * 200 / WATER_LEVEL_MAX);
  tft.fillRect(10, 180, 210, 20, TFT_BLACK);
  tft.drawRect(10, 180, 210, 20, TFT_WHITE);
  tft.fillRect(11, 181, barWidth, 18, TFT_BLUE);
  
  // Pumpenstatus anzeigen
  tft.setTextSize(2);
  tft.fillRect(90, 210, 150, 25, TFT_BLACK); // Bereich löschen
  tft.setCursor(90, 210);
  if (pumpActive) {
    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    tft.println("EIN");
  } else {
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.println("AUS");
  }
  
  // Schwellwerte anzeigen
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.setTextSize(1);
  tft.setCursor(10, 255);
  tft.printf("EIN: %.0fcm AUS: %.0fcm", PUMP_ON_LEVEL, PUMP_OFF_LEVEL);
  
  // ESP-NOW Status anzeigen
  tft.setTextSize(2);
  tft.fillRect(130, 270, 110, 25, TFT_BLACK);
  tft.setCursor(130, 270);
  if (espnowInitialized) {
    if (lastSendSuccess) {
      tft.setTextColor(TFT_GREEN, TFT_BLACK);
      tft.printf("OK #%d", espnowSendCount);
    } else {
      tft.setTextColor(TFT_YELLOW, TFT_BLACK);
      tft.printf("OK #%d", espnowSendCount);
    }
  } else {
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.println("FEHLER");
  }
  
  // Nächste ESP-NOW Übertragung anzeigen
  unsigned long nextESPNow = ESPNOW_SEND_INTERVAL - (currentTime - lastESPNowSend);
  int minutesLeftESPNow = nextESPNow / 60000;
  int secondsLeftESPNow = (nextESPNow % 60000) / 1000;
  tft.setTextSize(1);
  tft.setCursor(10, 300);
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.printf("Naechste ESP-NOW: %dm %ds  ", minutesLeftESPNow, secondsLeftESPNow);
  
  // Nächste Luftpumpen-Aktivierung anzeigen
  unsigned long nextAirPump = AIR_PUMP_INTERVAL - (currentTime - lastAirPumpTime);
  int minutesLeft = nextAirPump / 60000;
  int secondsLeft = (nextAirPump % 60000) / 1000;
  tft.setCursor(10, 310);
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.printf("Naechste Luftp.: %dm %ds  ", minutesLeft, secondsLeft);
  
  // Serielle Ausgabe
  Serial.printf("ADC: %d | Wasserstand: %.1f cm", adcValue, waterLevelCm);
  Serial.printf(" | Pumpe: %s\n", pumpActive ? "EIN" : "AUS");
  
  delay(500); // Aktualisierung alle 500 ms
}