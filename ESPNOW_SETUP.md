# ESP-NOW Setup - MAC-Adresse finden

## Schritt 1: MAC-Adresse der Wetterstation ermitteln

Lade folgenden Sketch auf die **Wetterstation**:

```cpp
#include <WiFi.h>

void setup() {
  Serial.begin(115200);
  WiFi.mode(WIFI_STA);
  
  Serial.println("\n=== ESP32 MAC-Adresse ===");
  Serial.print("MAC: ");
  Serial.println(WiFi.macAddress());
  
  // Als uint8_t Array für ESP-NOW
  uint8_t mac[6];
  WiFi.macAddress(mac);
  Serial.print("uint8_t weatherStationMAC[] = {");
  for (int i = 0; i < 6; i++) {
    Serial.printf("0x%02X", mac[i]);
    if (i < 5) Serial.print(", ");
  }
  Serial.println("};");
}

void loop() {
  delay(1000);
}
```

## Schritt 2: MAC-Adresse in main.cpp eintragen

Kopiere die Ausgabe und trage sie in **src/main.cpp** ein:

```cpp
// MAC-Adresse der Wetterstation (HIER ANPASSEN!)
uint8_t weatherStationMAC[] = {0x14, 0x33, 0x5C, 0x38, 0xD5, 0xD4};
```

## Schritt 3: Empfänger-Sketch auf Wetterstation

Lade diesen Sketch auf die **Wetterstation**, um Daten zu empfangen:

```cpp
#include <esp_now.h>
#include <WiFi.h>

// Datenstruktur (muss identisch mit Sender sein!)
typedef struct {
  float waterLevel;
  int adcValue;
  bool pumpActive;
  unsigned long uptime;
} WaterLevelData;

WaterLevelData receivedData;

// Callback-Funktion beim Empfang
void OnDataRecv(const uint8_t *mac, const uint8_t *incomingData, int len) {
  memcpy(&receivedData, incomingData, sizeof(receivedData));
  
  Serial.println("\n=== Zisternen-Daten empfangen ===");
  Serial.printf("Wasserstand: %.1f cm\n", receivedData.waterLevel);
  Serial.printf("ADC-Wert: %d\n", receivedData.adcValue);
  Serial.printf("Pumpe: %s\n", receivedData.pumpActive ? "EIN" : "AUS");
  Serial.printf("Betriebszeit: %lu Sekunden\n", receivedData.uptime);
  
  // Hier kannst du die Daten weiterverarbeiten
  // z.B. auf Display anzeigen, in Datenbank speichern, etc.
}

void setup() {
  Serial.begin(115200);
  WiFi.mode(WIFI_STA);
  
  Serial.print("Wetterstation MAC: ");
  Serial.println(WiFi.macAddress());
  
  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW Init Fehler!");
    return;
  }
  
  esp_now_register_recv_cb(OnDataRecv);
  Serial.println("ESP-NOW Empfänger bereit!");
}

void loop() {
  delay(100);
}
```

## Schritt 4: Test

1. **Wetterstation** mit Empfänger-Sketch starten
2. **Zisternen-Monitor** starten
3. Im Serial Monitor der **Zisternen-Monitor** sollte erscheinen:
   - `ESP-NOW erfolgreich initialisiert`
   - `Peer (Wetterstation) hinzugefügt: AA:BB:CC:DD:EE:FF`
4. Nach 15 Minuten (oder beim ersten Mal sofort) sendet der Monitor Daten
5. Im Serial Monitor der **Wetterstation** erscheinen die empfangenen Daten

## Fehlersuche

### "Fehler beim Hinzufügen des Peers"
- MAC-Adresse falsch eingetragen
- Überprüfe Format: `{0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF}`

### "ESP-NOW: Fehler beim Senden"
- Wetterstation nicht in Reichweite (max. ca. 200m im Freien)
- Wetterstation nicht gestartet
- Falsche MAC-Adresse

### Keine Daten empfangen
- Überprüfe, ob beide ESP32 die gleiche Datenstruktur verwenden
- Checke Serial Monitor beider Geräte

## Sendeintervall anpassen

In **src/main.cpp**:

```cpp
#define ESPNOW_SEND_INTERVAL 900000  // 15 Minuten (in ms)
```

Beispiele:
- 5 Minuten: `300000`
- 10 Minuten: `600000`
- 30 Minuten: `1800000`
- 1 Stunde: `3600000`
