#include <HTTPClient.h>
#include <WiFi.h>
#include "Ambient.h"
#include <time.h>

#define LED_PIN_R 21
#define LED_PIN_G 18
#define LED_PIN_B 19
#define SEN0193_PIN1 A0
#define SEN0193_PIN2 A3
#define SEN0193_PIN3 A6
#define RSMS_PIN1 A7
#define RSMS_PIN2 A4
#define RSMS_PIN3 A5
#define RELAY_PIN 23
#define SERIAL_BAND 115200
#define JST 3600*9

float THRESHOLD = 33.0;                // この値よりも大きくなったら潅水; area3と異なり更新することがあるのでdefineしない
#define SENSING_INTERVAL 1000 * 60     // この間隔で潅水状況をチェック
#define IRRIGATION_TIME 1000 * 60 * 5  // 潅水時間
#define IRRIGATION_WAIT 30             // この時間だけ前回の灌水から開ける(分)

int irrigation_countdown = 0;          // 一度灌水したら30分間灌水しないようにする; 毎回のloop()で1ずつ減らす

int csms1 = 0;
int csms2 = 0;
int csms3 = 0;
int csms_ave = 0;
int rsms1 = 0;
int rsms2 = 0;
int rsms3 = 0;
int rsms_ave = 0;

WiFiClient client;
const char* ssid2 = "WIFI_SSID";
const char* password2 = "WIFI_PASS";

Ambient ambient;
unsigned int irrigation_channelId = 00000;           // ambientのチャンネルID
const char* irrigation_writeKey = "YOUR_WRITE_KEY";  // ambientのライトキー
unsigned int sensor_channelId = 00000;               // ambientのチャンネルID
const char* sensor_writeKey = "YOUR_WRITE_KEY";      // ambientのライトキー
String server_url = "http://YOUR_IP_ADDRESS:YOUR_PORT/threshold";


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
  WiFi.begin(ssid2, password2);
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
  pinMode(RSMS_PIN1, INPUT);
  pinMode(RSMS_PIN2, INPUT);
  pinMode(RSMS_PIN3, INPUT);

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


int getThreshold() {
  HTTPClient http;

  if (!http.begin(client, server_url)) {
      Serial.println("Failed HTTPClient begin!");
      return 0;
  }

  Serial.println("HTTPClient begin!");
  http.addHeader("Content-Type", "application/json");
  int responseCode = http.GET();
  String body = http.getString();
  Serial.println(responseCode);
  Serial.println(body);

  http.end();

  // サーバから受信出来たらLED水色
  if (responseCode == 200) {
    digitalWrite(LED_PIN_R, LOW);
    digitalWrite(LED_PIN_G, HIGH);
    digitalWrite(LED_PIN_B, HIGH);
  }

  return body.toInt();
}


float returnVWC(float input_CSMS, float input_RSMS) {
  float bulk_ec = 2 * pow(10, 14) * pow(input_RSMS, -5.019);  // RSMS->バルクEC
  float delta_x = -77 * (exp(-10 * bulk_ec) + 1);             // バルクEC->差分delta_x 
  float xr = input_CSMS - delta_x;                            // delta_xをCSMSに加算
  float xr = input_CSMS;                 
  float theta = 38723 * pow(xr, -1.609);                      // CSMS->体積含水率
  
  return theta * 100;
}


bool checkIrrigationTime() {
  time_t t;
  struct tm *tm;

  t = time(NULL);
  tm = localtime(&t);

  if(tm->tm_hour >= 7 && tm->tm_hour < 15) {
    return true;
  } else {
    return false;
  }
}


void checkIsIrrigation(float threshold) {
  // Ambientの設定
  ambient.begin(irrigation_channelId, irrigation_writeKey, &client);

  float theta_count = 0;  // 正常に計測されたセンサ組数
  float theta_sum = 0;    // 合計値

  float theta1 = 0;
  float theta2 = 0;
  float theta3 = 0;

  // 測定値が1000を超えていたら正常
  if (csms1 > 1000 && rsms1 > 1000) { theta_count += 1; theta1 = returnVWC(csms1, rsms1); theta_sum += theta1; }
  if (csms2 > 1000 && rsms2 > 1000) { theta_count += 1; theta2 = returnVWC(csms2, rsms2); theta_sum += theta2; }
  if (csms3 > 1000 && rsms3 > 1000) { theta_count += 1; theta3 = returnVWC(csms3, rsms3); theta_sum += theta3; }

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

  // 閾値をスプレッドシートから問い合わせ
  if (threshold > 0) {
    THRESHOLD = threshold; // 正常に取得できたら上書き
  }
  Serial.println("Current THRESHOLD: " + String(THRESHOLD));
  ambient.set(6, THRESHOLD);

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


void sensingRSMS() {
  // Ambientの設定
  ambient.begin(sensor_channelId, sensor_writeKey, &client);

  int rsms_count = 0;  // 正常に計測されたセンサ数
  int rsms_sum = 0;    // 合計値

  rsms1 = analogRead(RSMS_PIN1);
  rsms2 = analogRead(RSMS_PIN2);
  rsms3 = analogRead(RSMS_PIN3);

  Serial.println("rsms1: " + String(rsms1));
  Serial.println("rsms2: " + String(rsms2));
  Serial.println("rsms3: " + String(rsms3));

  // 測定値が1000を超えていたら正常
  if (rsms1 > 1000) { rsms_count += 1; rsms_sum += rsms1; }
  if (rsms2 > 1000) { rsms_count += 1; rsms_sum += rsms2; }
  if (rsms3 > 1000) { rsms_count += 1; rsms_sum += rsms3; }

  // 平均値を計算
  rsms_ave = 0;
  if (rsms_count > 0) {
    rsms_ave = (int)(rsms_sum / rsms_count); // 基本的には0になることはない
  } else {
    // センサの値が正常に取れていないLED赤
    digitalWrite(LED_PIN_R, HIGH);
    digitalWrite(LED_PIN_G, LOW);
    digitalWrite(LED_PIN_B, LOW);
  }
  Serial.println("rsms_ave: " + String(rsms_ave));

  int ambient_status = 0;
  ambient.set(1, rsms1); 
  ambient.set(2, rsms2);
  ambient.set(3, rsms3);
  ambient.set(4, rsms_ave);
}


void sensingCSMS() {
  // Ambientの設定
  //ambient.begin(sensor_channelId, sensor_writeKey, &client); // RSMSですでに宣言済み

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
  ambient.set(5, csms1); 
  ambient.set(6, csms2);
  ambient.set(7, csms3);
  ambient.set(8, csms_ave);

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
  float th = float(getThreshold());
  Serial.println("Server Threshold: " + String(th));
  delay(1000);
  sensingRSMS();
  delay(1000);
  sensingCSMS();
  delay(1000);
  checkIsIrrigation(th);
  delay(SENSING_INTERVAL);
}