#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
#include <WiFi.h>
#include "Ambient.h"

#define LED_PIN_R 23
#define LED_PIN_G 18
#define LED_PIN_B 19
#define SERIAL_BAND 115200
#define SENSING_INTERVAL 1000 * 60

Adafruit_BME280 bme;

WiFiClient client;
const char* ssid = "WIFI_SSID";
const char* password = "WIFI_PASS";

Ambient ambient;
unsigned int channelId = 00000;           // ambientのチャンネルID
const char* writeKey = "YOUR_WRITE_KEY";  // ambientのライトキー

float temp;
float pres;
float humi;

void setup() {
  // LEDの初期化
  pinMode(LED_PIN_R, OUTPUT);
  pinMode(LED_PIN_G, OUTPUT);
  pinMode(LED_PIN_B, OUTPUT);

  // setup()の中：LED紫
  digitalWrite(LED_PIN_R, HIGH);
  digitalWrite(LED_PIN_G, LOW);
  digitalWrite(LED_PIN_B, HIGH);

  // シリアルモニタの初期化
  Serial.begin(SERIAL_BAND);
  while (!Serial) { ; } // wait for serial monitor
  Serial.println("Serial Ready at " + String(SERIAL_BAND));
  Serial.println();

  // BME280の初期化
  bool status;
  status = bme.begin(0x76);  
  while (!status) {
    Serial.println("BME280 preparing...");
    delay(1000);
  }

  // WiFiの初期化
  WiFi.begin(ssid, password);
  while(WiFi.status() != WL_CONNECTED) {
    Serial.print('.');
    delay(500);
  }
  Serial.println();
  Serial.printf("Connected, IP address: ");
  Serial.println(WiFi.localIP());

  // Ambientの設定
  ambient.begin(channelId, writeKey, &client);
}

void loop() { 
  temp=bme.readTemperature();
  humi=bme.readHumidity();
  pres=bme.readPressure() / 100.0F;

  Serial.println("temp: " + String(temp) + " C");
  Serial.println("humi: " + String(humi) + " %");
  Serial.println("pres: " + String(pres) + " hPa");

  ambient.set(1, temp); 
  ambient.set(2, humi);
  ambient.set(3, pres);
  ambient.send();

  int ambient_status = ambient.status;
  Serial.println(String(ambient_status));

  if(ambient_status == 200) {
    // Ambientに送信出来たらLED緑
    digitalWrite(LED_PIN_R, LOW);
    digitalWrite(LED_PIN_G, HIGH);
    digitalWrite(LED_PIN_B, LOW);
  } else {
    // Ambientに送信出来なかったらLED赤
    digitalWrite(LED_PIN_R, HIGH);
    digitalWrite(LED_PIN_G, LOW);
    digitalWrite(LED_PIN_B, LOW);
  }

  delay(SENSING_INTERVAL);
}