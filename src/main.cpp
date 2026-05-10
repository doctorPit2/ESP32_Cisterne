//ESP32_Meteo_Aussen

#include <Wire.h>
#include <SPI.h>
#include <Adafruit_Sensor.h>
#include "Adafruit_BME680.h"
#include <Adafruit_CCS811.h>
#include <WiFi.h>
//#include <WiFiMulti.h>  // NICHT MEHR BENÖTIGT - nur ESP-NOW, kein WiFi-Netzwerk
//#include "ESPAsyncWebServer.h"
#include <esp_now.h>
#include <esp_wifi.h>
#include <Tomoto_HM330X.h>
#include "DFRobot_RainfallSensor.h"

const byte DATAPIN = 19;    // TX20 Windmesser
const bool INVERSE_LOGIC = false;
const bool VERBOSE = false;

// KEIN WiFi-Netzwerk mehr - nur ESP-NOW auf Kanal 1
// WiFiMulti wifiMulti;  // ENTFERNT
// WiFi Credentials ENTFERNT - ESP32_Meteo_aussen braucht nur ESP-NOW


Tomoto_HM330X sensor; //Luft Partikel
DFRobot_RainfallSensor_I2C regen_Tic (&Wire); //RegenSensor
Adafruit_CCS811 ccs; //Luftqualität eCO2 und TVOC

// Eigene MAC-Adresse: B4:BF:E9:14:31:E8
// WiFi-Kanal: fest auf 1 eingestellt

// MAC-Adresse des Empfängers Innen
uint8_t broadcastAddress1[] = {0x1C, 0xC3, 0xAB, 0xC4, 0x33, 0x1C};

// MAC-Adresse des RSSI Monitors
uint8_t broadcastAddress2[] = {0xB0, 0xCB, 0xD8, 0x02, 0xFD, 0x08}; //TFT RSSI

// Replace with your network credentials
// WiFi-Kanal ist fest auf 1 eingestellt (keine automatische Anpassung mehr nötig)

typedef struct struct_message {
  float temperature;
  float humidity;
  float pressure;
  float gasResistance;
  int16_t wind_speed;
  uint16_t wind_dir;
  uint8_t sensorNummer;
  
  float std_PM1;      //Staubsensorwerte standard particlate matter (ug/m^3) --")
  float std_PM2_5;
  float std_PM10;

  float atm_PM1;      //Staubsensorwerte atmospheric environment (ug/m^3) --")
  float atm_PM2_5;
  float atm_PM10;

  float rainFall;
  float rainfall1;
  float workingTime;
  float rawData;

  uint16_t eco2;      // CCS811 eCO2 in ppm
  uint16_t tvoc;      // CCS811 TVOC in ppb

} struct_message;

// Create a struct_message called myData
struct_message myData;

//Uncomment if using SPI
/*#define BME_SCK 18
#define BME_MISO 19
#define BME_MOSI 23
#define BME_CS 5*/

esp_now_peer_info_t peerInfo;
/*
// callback when data is sent
void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  Serial.print("\r\nLast Packet Send Status:\t");
  Serial.println(status == ESP_NOW_SEND_SUCCESS ? "Delivery Success" : "Delivery Fail");

}
*/

void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  // Callback - keine Ausgabe
}

Adafruit_BME680 bme; // I2C
//Adafruit_BME680 bme(BME_CS); // hardware SPI
//Adafruit_BME680 bme(BME_CS, BME_MOSI, BME_MISO, BME_SCK);

float temperature;
float humidity;
float pressure;
float gasResistance;

uint16_t eco2;  // CCS811 eCO2 in ppm
uint16_t tvoc;  // CCS811 TVOC in ppb

float std_PM1;      //Staubsensorwerte standard particlate matter (ug/m^3) --")
float std_PM2_5;
float std_PM10;

float atm_PM1;      //Staubsensorwerte atmospheric environment (ug/m^3) --")
float atm_PM2_5;
float atm_PM10;

float rainFall;
float rainfall1;
float workingTime;
float rawData;

const uint32_t NO_DATA_TIMEOUT_MS = 5000;
String raw_data_buff = "";
uint64_t time_ref_us;
uint64_t last_event_time_ms = 0;
uint16_t wind_speed;
uint16_t wind_dir;

unsigned long lastTime = 0;  
unsigned long timerDelay = 1000;  // send readings timer

void getSensorReadings(){         //Staubsensor auslesen HM3301

  if (!sensor.readSensor()) {
    // Read failed - ignore
  } else {
    
    std_PM1 = sensor.std.getPM1();      //Staubsensorwerte standard particlate matter (ug/m^3) --")
    std_PM2_5 = sensor.std.getPM2_5();
    std_PM10 = sensor.std.getPM10();

    atm_PM1 = sensor.atm.getPM1();      //Staubsensorwerte atmospheric environment (ug/m^3) --")
    atm_PM2_5 = sensor.atm.getPM2_5();
    atm_PM10 = sensor.atm.getPM10(); 
    
  }

}
void get_regenDaten(){

 workingTime = regen_Tic.getSensorWorkingTime();
 rainFall = regen_Tic.getRainfall();
 rainfall1 = regen_Tic.getRainfall(1);
 rawData = regen_Tic.getRawData();
  


}

void getCCS811Readings(){     // CCS811 Luftqualität auslesen
  if(ccs.available()){
    if(!ccs.readData()){
      eco2 = ccs.geteCO2();
      tvoc = ccs.getTVOC();
    } else {
      eco2 = 0;
      tvoc = 0;
    }
  } else {
    // Sensor noch nicht bereit
    eco2 = 0;
    tvoc = 0;
  }
}

void getBME680Readings(){     //BME680 auslesen
  // Tell BME680 to begin measurement.
  unsigned long endTime = bme.beginReading();
  if (endTime == 0) {
    return;
  }
  if (!bme.endReading()) {
    return;
  }
  temperature = bme.temperature - 10.0;  // Korrektur: Gas-Heater erwärmt Sensor um ~10°C
  pressure = bme.pressure / 100.0;
  humidity = bme.humidity;
  gasResistance = bme.gas_resistance / 1000.0;
}

void setup() {    //////////////////////SETUP/////////////////////////////////////
  // Init Serial Monitor
  Serial.begin(115200);
  
  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(DATAPIN, INPUT);
  last_event_time_ms = millis();
  
  // Staubsensor initialisieren
  if (!sensor.begin()) {
    while (1);
  }
  
  // WiFi im STA-Modus starten (für ESP-NOW nötig, KEINE Netzwerk-Verbindung)
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false); // Deaktiviert Power-Save
  WiFi.disconnect();    // Keine WiFi-Verbindung aufbauen!
  
  // MAC-Adresse dieses ESP32 anzeigen
  Serial.print("ESP32 MAC-Adresse: ");
  Serial.println(WiFi.macAddress());
  
  // Externe Antenne für ESP32-WROOM-32U aktivieren (U.FL-Anschluss)
  // ANT1 = externe Antenne, ANT0 = interne PCB-Antenne
  wifi_ant_config_t ant_config;
  ant_config.rx_ant_mode = WIFI_ANT_MODE_ANT1;      // Externe Antenne für Empfang
  ant_config.rx_ant_default = WIFI_ANT_ANT1;        // Standard: externe Antenne
  ant_config.tx_ant_mode = WIFI_ANT_MODE_ANT1;      // Externe Antenne für Senden
  ant_config.enabled_ant0 = 0;                       // Interne Antenne deaktiviert
  ant_config.enabled_ant1 = 1;                       // Externe Antenne aktiviert
  esp_wifi_set_ant(&ant_config);
  
  // Standard WiFi-Protokolle (802.11b/g/n) für maximale Kompatibilität
  esp_wifi_set_protocol(WIFI_IF_STA, WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N);
  
  // Kanal fest auf 1 setzen für ESP-NOW
  uint8_t currentPrimaryChannel = 1;
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_channel(currentPrimaryChannel, WIFI_SECOND_CHAN_NONE);
  esp_wifi_set_promiscuous(false);
  
  wifi_second_chan_t currentSecondChannel = WIFI_SECOND_CHAN_NONE;
  esp_wifi_get_channel(&currentPrimaryChannel, &currentSecondChannel);
  
  // TX Power auf Maximum setzen
  WiFi.setTxPower(WIFI_POWER_19_5dBm);
  
  memset(&peerInfo, 0, sizeof(peerInfo));
  peerInfo.channel = currentPrimaryChannel;
  peerInfo.ifidx = WIFI_IF_STA;
  peerInfo.encrypt = false;
  
  // Init ESP-NOW
  if (esp_now_init() != ESP_OK) {
    Serial.println("Error initializing ESP-NOW");
    return;
  }
  
  // I2C Bus initialisieren (wichtig für BME680!)
  Wire.begin();
  
  // Init BME680 sensor
  if (!bme.begin()) {
    while (1);
  }
  // Set up oversampling and filter initialization
  bme.setTemperatureOversampling(BME680_OS_8X);
  bme.setHumidityOversampling(BME680_OS_2X);
  bme.setPressureOversampling(BME680_OS_4X);
  bme.setIIRFilterSize(BME680_FILTER_SIZE_3);
  bme.setGasHeater(0, 0); // Gas-Heater deaktiviert (haben CCS811 für Luftqualität)
  
  // Init CCS811 sensor
  if(!ccs.begin()){
    // Kein while(1) - System läuft auch ohne CCS811 weiter
  } else {
    // Warte bis Sensor bereit ist
    while(!ccs.available()){
      delay(100);
    }
  }
  
  // Register send callback
  esp_now_register_send_cb(OnDataSent);
  
  // Register peer 1 (Empfänger Innen)
  memcpy(peerInfo.peer_addr, broadcastAddress1, 6);
  if (esp_now_add_peer(&peerInfo) != ESP_OK){
    Serial.println("Failed to add peer 1");
    return;
  }
  
  // Register peer 2 (RSSI Monitor)
  memcpy(peerInfo.peer_addr, broadcastAddress2, 6);
  if (esp_now_add_peer(&peerInfo) != ESP_OK){
    Serial.println("Failed to add peer 2");
    return;
  }
}
 


void wait_rel_us(uint64_t rel_time_us) {
  uint64_t elapsed_us = micros() - time_ref_us;
  if (elapsed_us < rel_time_us) {
    delayMicroseconds(rel_time_us - elapsed_us);
  }
  time_ref_us += rel_time_us;
}

boolean parse_data() {
  int bitcount = 0;
  raw_data_buff = "";
  uint8_t buff_pointer = 0;
  uint64_t bit_buff = 0;

  for (bitcount = 41; bitcount > 0; bitcount--) {
    uint8_t pin = digitalRead(DATAPIN);
    if (INVERSE_LOGIC) {
      if (!pin) {
        raw_data_buff += "1";
        bit_buff += (uint64_t)1 << buff_pointer;
      } else {
        raw_data_buff += "0";
      }
    } else {
      if (pin) {
        raw_data_buff += "1";
        bit_buff += (uint64_t)1 << buff_pointer;
      } else {
        raw_data_buff += "0";
      }
    }
    buff_pointer++;
    wait_rel_us(1220);
  }


 uint8_t start = bit_buff & 0b11111;
  if (start != 0b11011) {
    if (VERBOSE) {
      char a[90];
      sprintf(a, "wrong start frame (%d != %d)", start, 0b11011);
      Serial.println(a);
      Serial.println(raw_data_buff);
    }
    return false;
  }

  uint8_t dir = (~(bit_buff >> 5)) & 0b1111;
  uint16_t speed = (~(bit_buff >> (5 + 4))) & 0b111111111111;
  uint8_t chk = (~(bit_buff >> (5 + 4 + 12))) & 0b1111;
  uint8_t dir2 = (bit_buff >> (5 + 4 + 12 + 4)) & 0b1111;
  uint16_t speed2 = (bit_buff >> (5 + 4 + 12 + 4 + 4)) & 0b111111111111;

  uint8_t chk_calc = (dir + (speed & 0b1111) + ((speed >> 4) & 0b1111) +
                      ((speed >> 8) & 0b1111)) &
                     0b1111;
  if (dir != dir2) {
    if (VERBOSE) {
      char a[90];
      sprintf(a, "cwind speed inconsistent (%d != %d)", speed, speed2);
      Serial.println(a);
      Serial.println(raw_data_buff);
    }
    return false;
  }
  if (speed != speed2) {
    if (VERBOSE) {
      char a[90];
      sprintf(a, "cwind speed inconsistent (%d != %d)", speed, speed2);
      Serial.println(a);
      Serial.println(raw_data_buff);
    }
    return false;
  }
  if (chk_calc != chk) {
    if (VERBOSE) {
      char a[90];
      sprintf(a, "checksum incorrect (%d != %d)", chk_calc, chk);
      Serial.println(a);
      Serial.println(raw_data_buff);
    }
    return false;
  }
  wind_speed = speed;
  wind_dir = dir * 225;
  wind_dir = wind_dir / 10;

  return true;
}

void loop() { //////////////////////////Loop/////////////////////////////////////////
  // WiFi-Kanal ist fest auf Kanal 1 eingestellt - keine Anpassung mehr erforderlich
  
  if ((digitalRead(DATAPIN) && !INVERSE_LOGIC) or
      (!digitalRead(DATAPIN) && INVERSE_LOGIC)) {
    time_ref_us = micros();
    wait_rel_us(300);  // wait to prevent measuring on the edge of the signals
    char a[90];
    boolean validData = parse_data();
    if (!validData) {
      // fail - ignore
    } else {
      digitalWrite(LED_BUILTIN, HIGH);
      sprintf(a, "ok,%d,%d", wind_dir, wind_speed);
      digitalWrite(LED_BUILTIN, LOW);
    }
    delay(10);  // to prevent getting triggered again
    last_event_time_ms = millis();
  }
  if (millis() - last_event_time_ms >= NO_DATA_TIMEOUT_MS) {
    last_event_time_ms = millis();
  }
 
 
  if ((millis() - lastTime) > timerDelay) {
    
    get_regenDaten();
    getSensorReadings();
    getBME680Readings();
    getCCS811Readings();
    
    
    
    // Set values to send
  myData.temperature = temperature;
  myData.humidity = humidity;
  myData.pressure =  pressure;
  myData.gasResistance =  gasResistance;
  myData.wind_speed = wind_speed;
  myData.wind_dir = wind_dir;

  myData.std_PM1 = std_PM1;
  myData.std_PM2_5 = std_PM2_5;
  myData.std_PM10 = std_PM10;

  myData.atm_PM1   = atm_PM1;
  myData.atm_PM2_5 = atm_PM2_5;
  myData.atm_PM10 = atm_PM10;

myData.workingTime = workingTime ;
myData.rainFall = rainFall;
myData.rainfall1 = rainfall1 ;
myData.rawData = rawData;

myData.eco2 = eco2;
myData.tvoc = tvoc;

  // Send message via ESP-NOW to Empfänger Innen
  esp_err_t result = esp_now_send(broadcastAddress1, (uint8_t *) &myData, sizeof(myData));

  delay(200);
  
  // Send message via ESP-NOW to RSSI Monitor
  esp_err_t result2 = esp_now_send(broadcastAddress2, (uint8_t *) &myData, sizeof(myData));
 
 
 
  // delay(2000);
    
    
    
    lastTime = millis();
  }
}