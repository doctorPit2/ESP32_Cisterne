#include <Arduino.h>
#include <TFT_eSPI.h>

// TFT Display initialisieren
TFT_eSPI tft = TFT_eSPI();

// Drucksensor MPX5050 am GPIO 35 (ADC1 CH7)
#define PRESSURE_SENSOR_PIN 35

// MOSFET für Pumpenrelais am GPIO 17
#define PUMP_MOSFET_PIN 17

// Kalibrierungswerte (anpassen nach Bedarf)
#define ADC_MIN 0          // ADC-Wert bei 0 cm Wasserstand
#define ADC_MAX 4095       // ADC-Wert bei maximalem Wasserstand
#define WATER_LEVEL_MAX 300 // Maximale Füllhöhe in cm (3 Meter)

// Pumpensteuerung (Hysterese)
#define PUMP_ON_LEVEL 30.0   // Pumpe EINschalten bei 30 cm
#define PUMP_OFF_LEVEL 15.0  // Pumpe AUSschalten bei 15 cm

// Globale Variablen
bool pumpActive = false;

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
  
  // Display initialisieren
  tft.init();
  tft.setRotation(1); // Landscape-Modus (0-3 möglich)
  
  // Backlight einschalten
  pinMode(27, OUTPUT);
  digitalWrite(27, HIGH);
  
  // Hintergrund schwarz
  tft.fillScreen(TFT_BLACK);
  
  // Überschrift
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.setTextSize(3);
  tft.setCursor(10, 10);
  tft.println("Zisternen-Monitor");
  
  // Statische Beschriftungen
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(2);
  tft.setCursor(10, 60);
  tft.println("ADC-Wert:");
  
  tft.setCursor(10, 120);
  tft.println("Wasserstand:");
  
  tft.setCursor(10, 240);
  tft.println("Pumpe:");
  
  Serial.println("Display initialisiert!");
}

void loop() {
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
  
  // ADC-Wert anzeigen
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.setTextSize(3);
  tft.fillRect(10, 90, 460, 25, TFT_BLACK); // Bereich löschen
  tft.setCursor(10, 90);
  tft.printf("%4d / 4095", adcValue);
  
  // Wasserstand anzeigen
  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  tft.setTextSize(4);
  tft.fillRect(10, 150, 460, 35, TFT_BLACK); // Bereich löschen
  tft.setCursor(10, 150);
  tft.printf("%.1f cm", waterLevelCm);
  
  // Fortschrittsbalken (0-300 cm)
  int barWidth = (int)(waterLevelCm * 400 / WATER_LEVEL_MAX);
  tft.fillRect(10, 200, 400, 30, TFT_BLACK);
  tft.drawRect(10, 200, 400, 30, TFT_WHITE);
  tft.fillRect(11, 201, barWidth, 28, TFT_BLUE);
  
  // Pumpenstatus anzeigen
  tft.setTextSize(3);
  tft.fillRect(150, 240, 320, 30, TFT_BLACK); // Bereich löschen
  tft.setCursor(150, 240);
  if (pumpActive) {
    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    tft.println("EIN (Pumpt)");
  } else {
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.println("AUS");
  }
  
  // Schwellwerte anzeigen
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.setTextSize(1);
  tft.setCursor(10, 280);
  tft.printf("EIN: %.0f cm | AUS: %.0f cm", PUMP_ON_LEVEL, PUMP_OFF_LEVEL);
  
  // Serielle Ausgabe
  Serial.printf("ADC: %d | Wasserstand: %.1f cm", adcValue, waterLevelCm);
  Serial.printf(" | Pumpe: %s\n", pumpActive ? "EIN" : "AUS");
  
  delay(500); // Aktualisierung alle 500 ms
}