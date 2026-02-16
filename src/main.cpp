//ESP32_Meteo_Aussen

#include <Wire.h>
#include <SPI.h>
#include <Adafruit_Sensor.h>
#include "Adafruit_BME680.h"
#include <WiFi.h>
//#include "ESPAsyncWebServer.h"
#include <esp_now.h>
#include <Tomoto_HM330X.h>


const byte DATAPIN = 19;
const bool INVERSE_LOGIC = false;
const bool VERBOSE = false;

Tomoto_HM330X sensor;

uint8_t broadcastAddress[] = {0x08, 0xA6, 0xF7, 0x66, 0x28, 0x64};
// Replace with your network credentials
typedef struct struct_message {
  float temperature;
  float humidity;
  float pressure;
  float gasResistance;
  int16_t wind_speed;
  uint16_t wind_dir;
  u8_t sensorNummer;
  
  float std_PM1;      //Staubsensorwerte standard particlate matter (ug/m^3) --")
  float std_PM2_5;
  float std_PM10;

  float atm_PM1;      //Staubsensorwerte atmospheric environment (ug/m^3) --")
  float atm_PM2_5;
  float atm_PM10;

} struct_message;

// Create a struct_message called myData
struct_message myData;

//Uncomment if using SPI
/*#define BME_SCK 18
#define BME_MISO 19
#define BME_MOSI 23
#define BME_CS 5*/
esp_now_peer_info_t peerInfo;

// callback when data is sent
void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  Serial.print("\r\nLast Packet Send Status:\t");
  Serial.println(status == ESP_NOW_SEND_SUCCESS ? "Delivery Success" : "Delivery Fail");
}
Adafruit_BME680 bme; // I2C
//Adafruit_BME680 bme(BME_CS); // hardware SPI
//Adafruit_BME680 bme(BME_CS, BME_MOSI, BME_MISO, BME_SCK);

float temperature;
float humidity;
float pressure;
float gasResistance;

float std_PM1;      //Staubsensorwerte standard particlate matter (ug/m^3) --")
float std_PM2_5;
float std_PM10;;

float atm_PM1;      //Staubsensorwerte atmospheric environment (ug/m^3) --")
float atm_PM2_5;
float atm_PM10;

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
    Serial.println("Failed to read HM330X");
  } else {
    
    std_PM1 = sensor.std.getPM1();      //Staubsensorwerte standard particlate matter (ug/m^3) --")
    std_PM2_5 = sensor.std.getPM2_5();
    std_PM10 = sensor.std.getPM10();

    atm_PM1 = sensor.atm.getPM1();      //Staubsensorwerte atmospheric environment (ug/m^3) --")
    atm_PM2_5 = sensor.atm.getPM1();
    atm_PM10 = sensor.atm.getPM1(); 
    
  }

}

void getBME680Readings(){     //BME680 auslesen
  // Tell BME680 to begin measurement.
  unsigned long endTime = bme.beginReading();
  if (endTime == 0) {
    Serial.println(F("Failed to begin reading :("));
    return;
  }
  if (!bme.endReading()) {
    Serial.println(F("Failed to complete reading :("));
    return;
  }
  temperature = bme.temperature;
  pressure = bme.pressure / 100.0;
  humidity = bme.humidity;
  gasResistance = bme.gas_resistance / 1000.0;
}

void setup() {
  // Init Serial Monitor
  Serial.begin(115200);
  
  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(DATAPIN, INPUT);
  Serial.begin(115200);
  Serial.println("starting, direction in ddeg, speed in dm/s");
  last_event_time_ms = millis();
  // Set device as a Wi-Fi Station
 
 if (!sensor.begin()) {
    Serial.println("Failed to initialize HM330X");
    while (1)
      ;
  }

  Serial.println("HM330X initialized");
  Serial.println();



  WiFi.mode(WIFI_STA);

  // Init ESP-NOW
  if (esp_now_init() != ESP_OK) {
    Serial.println("Error initializing ESP-NOW");
    return;
  }
  // Init BME680 sensor
  if (!bme.begin()) {
    Serial.println(F("Could not find a valid BME680 sensor, check wiring!"));
    while (1);
  }
  // Set up oversampling and filter initialization
  bme.setTemperatureOversampling(BME680_OS_8X);
  bme.setHumidityOversampling(BME680_OS_2X);
  bme.setPressureOversampling(BME680_OS_4X);
  bme.setIIRFilterSize(BME680_FILTER_SIZE_3);
  bme.setGasHeater(320, 150); // 320*C for 150 ms
  // Once ESPNow is successfully Init, we will register for Send CB to
  // get the status of Trasnmitted packet
  esp_now_register_send_cb(OnDataSent);
  
  // Register peer
  memcpy(peerInfo.peer_addr, broadcastAddress, 6);
  peerInfo.channel = 0;  
  peerInfo.encrypt = false;
  
  // Add peer        
  if (esp_now_add_peer(&peerInfo) != ESP_OK){
    Serial.println("Failed to add peer");
    return;
  }
}
 


void wait_rel_us(uint64_t rel_time_us) {
  delayMicroseconds(rel_time_us - (micros() - time_ref_us));
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
  return true;
}

void loop() {
 if ((digitalRead(DATAPIN) && !INVERSE_LOGIC) or
      (!digitalRead(DATAPIN) && INVERSE_LOGIC)) {
    time_ref_us = micros();
    wait_rel_us(300);  // wait to prevent measuring on the edge of the signals
    char a[90];
    boolean validData = parse_data();
    if (!validData) {
      Serial.println("fail,could not parse data");
      delay(1000);
    } else {
      digitalWrite(LED_BUILTIN, HIGH);
      sprintf(a, "ok,%d,%d", wind_dir, wind_speed);
      Serial.println(a);
      digitalWrite(LED_BUILTIN, LOW);
    }
    delay(10);  // to prevent getting triggered again
    last_event_time_ms = millis();
  }
  if (millis() - last_event_time_ms >= NO_DATA_TIMEOUT_MS) {
    Serial.println("fail,timout");
    last_event_time_ms = millis();
  }
 
 
  if ((millis() - lastTime) > timerDelay) {
    
    getSensorReadings();
    getBME680Readings();
    Serial.printf("Temperature = %.2f ºC \n", temperature);
    Serial.printf("Humidity = %.2f % \n", humidity);
    Serial.printf("Pressure = %.2f hPa \n", pressure);
    Serial.printf("Gas Resistance = %.2f KOhm \n", gasResistance);
    Serial.printf("Wind_Speed = %.2u Kmh \n", wind_speed);
    Serial.printf("Wind_Direction = %.2u Grad \n", wind_dir);
    
    Serial.printf("standard particlate matter 1.0 (ug/m^3) = %.2f ug/m^3\n", std_PM1);
    Serial.printf("standard particlate matter 2.5 (ug/m^3) = %.2f ug/m^3\n", std_PM2_5);
    Serial.printf("standard particlate matter 10 (ug/m^3) = %.2f ug/m^3\n", std_PM10);
    Serial.println();
    Serial.printf("atmospheric environment (ug/m^3) = %.2f ug/m^3\n", atm_PM1);
    Serial.printf("atmospheric environment 2.5 (ug/m^3) = %.2f ug/m^3\n", atm_PM2_5);
    Serial.printf("atmospheric environment 10 (ug/m^3) = %.2f ug/m^3\n", atm_PM10);
    Serial.println();
    
    
    
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

  // Send message via ESP-NOW
  esp_err_t result = esp_now_send(broadcastAddress, (uint8_t *) &myData, sizeof(myData));
   
  if (result == ESP_OK) {
    Serial.println("Sent with success");
  }
  else {
    Serial.println("Error sending the data");
  }
 // delay(2000);
    
    
    
    lastTime = millis();
  }
}