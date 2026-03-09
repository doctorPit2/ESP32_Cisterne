#include <Arduino.h>
#include <TFT_eSPI.h>

// TFT Display initialisieren
TFT_eSPI tft = TFT_eSPI();

// Drucksensor MPX5050 am GPIO 35 (ADC1 CH7)
#define PRESSURE_SENSOR_PIN 35

// Kalibrierungswerte (anpassen nach Bedarf)
#define ADC_MIN 0          // ADC-Wert bei 0 cm Wasserstand
#define ADC_MAX 4095       // ADC-Wert bei maximalem Wasserstand
#define WATER_LEVEL_MAX 300 // Maximale Füllhöhe in cm (3 Meter)

void setup() {
  Serial.begin(115200);
  Serial.println("Zisternen-Monitor gestartet...");
  
  // ADC-Pin konfigurieren
  pinMode(PRESSURE_SENSOR_PIN, INPUT);
  analogReadResolution(12); // 12-Bit ADC (0-4095)
  analogSetAttenuation(ADC_11db); // 0-3,3V Messbereich
  
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
  
  // Serielle Ausgabe
  Serial.printf("ADC: %d | Wasserstand: %.1f cm\n", adcValue, waterLevelCm);
  
  delay(500); // Aktualisierung alle 500 ms
}