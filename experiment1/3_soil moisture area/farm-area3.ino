#include <Wire.h>
#include <WiFi.h>
#include "Ambient.h"
#include <time.h>

#define LED_PIN_R 21
#define LED_PIN_G 18
#define LED_PIN_B 19
#define SEN0193_PIN1 A0
#define SEN0193_PIN2 A3
#define SEN0193_PIN3 A6
#define RELAY_PIN 23
#define SERIAL_BAND 115200
#define JST 3600*9

#define SENSING_INTERVAL 1000 * 60     // この間隔で潅水状況をチェック
#define IRRIGATION_TIME 1000 * 60 * 5  // 潅水時間
#define IRRIGATION_WAIT 30             // この時間だけ前回の灌水から開ける(分)
float THRESHOLD = 33.0;                // この値よりも小さくなったら潅水(%)

int irrigation_countdown = 0;          // 一度灌水したら30分間灌水しないようにする; 毎回のloop()で1ずつ減らす

int csms1 = 0;
int csms2 = 0;
int csms3 = 0;
int csms_ave = 0;

WiFiClient client;
const char* ssid = "WIFI_SSID";
const char* password = "WIFI_PASS";

Ambient ambient;
unsigned int irrigation_channelId = 00000;           // ambientのチャンネルID
const char* irrigation_writeKey = "YOUR_WRITE_KEY";  // ambientのライトキー
unsigned int sensor_channelId = 00000;               // ambientのチャンネルID
const char* sensor_writeKey = "YOUR_WRITE_KEY";      // ambientのライトキー


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

  // WiFiの初期化
  WiFi.begin(ssid, password);
  while(WiFi.status() != WL_CONNECTED) {
    Serial.print('.');
    delay(500);
  }
  Serial.println();
  Serial.printf("Connected, IP address: ");
  Serial.println(WiFi.localIP());

  // センサの初期化
  pinMode(SEN0193_PIN1, INPUT);
  pinMode(SEN0193_PIN2, INPUT);
  pinMode(SEN0193_PIN3, INPUT);

  // リレー回路の初期化
  pinMode(RELAY_PIN, OUTPUT);

  // JSTで時間を調整
  configTime(JST, 0, "ntp.nict.jp", "ntp.jst.mfeed.ad.jp");
}


void irrigation() {
  // LED青
  digitalWrite(LED_PIN_R, LOW);
  digitalWrite(LED_PIN_G, LOW);
  digitalWrite(LED_PIN_B, HIGH);

  digitalWrite(RELAY_PIN, HIGH);
  Serial.println("irrigation start");
  delay(IRRIGATION_TIME);
  digitalWrite(RELAY_PIN, LOW);
  Serial.println("irrigation end");

  // LED緑
  digitalWrite(LED_PIN_R, LOW);
  digitalWrite(LED_PIN_G, HIGH);
  digitalWrite(LED_PIN_B, LOW);

  // n分間灌水しないようにする
  irrigation_countdown = IRRIGATION_WAIT;
}


float returnVWC(float input_CSMS) {
  float theta = 38723 * pow(input_CSMS, -1.609);             
  return theta * 100;
}


bool checkIrrigationTime() {
  time_t t;
  struct tm *tm;

  t = time(NULL);
  tm = localtime(&t);

  Serial.println(tm->tm_hour);

  if(tm->tm_hour >= 7 && tm->tm_hour < 15) {
    return true;
  } else {
    return false;
  }
}


void checkIsIrrigation() {
  // Ambientの設定
  ambient.begin(irrigation_channelId, irrigation_writeKey, &client);

  float theta_count = 0;  // 正常に計測されたセンサ組数
  float theta_sum = 0;    // 合計値

  float theta1 = 0;
  float theta2 = 0;
  float theta3 = 0;

  // 測定値が1000を超えていたら正常
  if (csms1 > 1000) { theta_count += 1; theta1 = returnVWC(csms1); theta_sum += theta1; }
  if (csms2 > 1000) { theta_count += 1; theta2 = returnVWC(csms2); theta_sum += theta2; }
  if (csms3 > 1000) { theta_count += 1; theta3 = returnVWC(csms3); theta_sum += theta3; }

  Serial.println("theta1: " + String(theta1));
  Serial.println("theta2: " + String(theta2));
  Serial.println("theta3: " + String(theta3));

  // 平均値を計算
  float theta_ave = 0;
  if (theta_count > 0) {
    theta_ave = theta_sum / theta_count; // 基本的には0になることはない
  } else {
    // センサの値が正常に取れていないLED赤
    digitalWrite(LED_PIN_R, HIGH);
    digitalWrite(LED_PIN_G, LOW);
    digitalWrite(LED_PIN_B, LOW);
  }
  Serial.println("theta_ave: " + String(theta_ave));

  int ambient_status = 0;
  ambient.set(1, theta1); 
  ambient.set(2, theta2);
  ambient.set(3, theta3);
  ambient.set(4, theta_ave);

  // 潅水条件
  if (theta_ave < THRESHOLD && irrigation_countdown == 0 && checkIrrigationTime() == true) { 
    irrigation(); 
    ambient.set(5, 1);
  } else {
    ambient.set(5, 0);
  }
  ambient.send();
  ambient_status = ambient.status;

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
}


void sensingCSMS() {
  // Ambientの設定
  ambient.begin(sensor_channelId, sensor_writeKey, &client);

  int csms_count = 0;  // 正常に計測されたセンサ数
  int csms_sum = 0;    // 合計値

  csms1 = analogRead(SEN0193_PIN1);
  csms2 = analogRead(SEN0193_PIN2);
  csms3 = analogRead(SEN0193_PIN3);

  Serial.println("csms1: " + String(csms1));
  Serial.println("csms2: " + String(csms2));
  Serial.println("csms3: " + String(csms3));

  // 測定値が1000を超えていたら正常
  if (csms1 > 1000) { csms_count += 1; csms_sum += csms1; }
  if (csms2 > 1000) { csms_count += 1; csms_sum += csms2; }
  if (csms3 > 1000) { csms_count += 1; csms_sum += csms3; }

  // 平均値を計算
  csms_ave = 0;
  if (csms_count > 0) {
    csms_ave = (int)(csms_sum / csms_count); // 基本的には0になることはない
  } else {
    // センサの値が正常に取れていないLED赤
    digitalWrite(LED_PIN_R, HIGH);
    digitalWrite(LED_PIN_G, LOW);
    digitalWrite(LED_PIN_B, LOW);
  }
  Serial.println("csms_ave: " + String(csms_ave));

  int ambient_status = 0;
  ambient.set(1, csms1); 
  ambient.set(2, csms2);
  ambient.set(3, csms3);
  ambient.set(4, csms_ave);

  ambient.send();
  ambient_status = ambient.status;

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
}


void loop() {
  if(irrigation_countdown > 0) { irrigation_countdown--; }
  sensingCSMS();
  delay(1000);
  checkIsIrrigation();
  delay(SENSING_INTERVAL);
}


