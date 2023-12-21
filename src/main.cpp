#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <time.h>
#include <credential.h>

#define TRG 12
#define ECH 13
#define HIGHTIME 10
#define threshold 500   // 50cm
#define judge_time 1000 // 1秒

HTTPClient http;

struct tm timeInfo; // 時刻を格納するオブジェクト
int before_hour = 0;
int start_day = 0;
int start_hour;

const int bufferSize = 5;
int buffer[bufferSize];
int bufferIndex = 0;
int total = 0;
int count = 0;

void addData(int value)
{
  total -= buffer[bufferIndex];
  buffer[bufferIndex] = value;
  total += value;
  bufferIndex = (bufferIndex + 1) % bufferSize;
  if (count < bufferSize)
  {
    count++;
  }
}

int getMovingAverage()
{
  if (count == 0)
    return 0;           // データがまだない場合
  return total / count; // 移動平均を計算
}

unsigned long calculateDistance(unsigned long timeMicroseconds)
{
  const double temp = 26.0;
  return (timeMicroseconds / 2) * 0.3466; // 丸め誤差などにより0になってしまうため先に計算（331m/s + 0.6 * 26 * / 1000000μs * 1000mm）mm/s
}

float responseEcho()
{
  float dis;
  unsigned long diff;

  delay(10);
  digitalWrite(TRG, HIGH);
  delayMicroseconds(HIGHTIME);
  digitalWrite(TRG, LOW);
  diff = pulseIn(ECH, HIGH);
  return calculateDistance((float)diff);
}

int prevMovingAverage = 0;
bool isLargeAmountChange()
{
  // Serial.println("3");
  bool result = false;

  for (int j = 0; j < bufferSize; j++)
  {
    float dis = responseEcho();
    if ((int)dis > 1200)
    {
      dis = 1200;
    }
    Serial.print("dis: ");
    Serial.println((int)dis);
    addData((int)dis);
  }
  const int avg = getMovingAverage();
  bool isChange = abs(prevMovingAverage - avg) >= 150;
  if (prevMovingAverage == 0)
  {
    isChange = false;
  }
  prevMovingAverage = avg;
  return isChange;
}

void sendSlackMessage(String webHookUrl, String text)
{
  char json_string[255];
  StaticJsonDocument<200> doc;
  doc["text"] = text;
  serializeJson(doc, json_string, sizeof(json_string));
  HTTPClient http;
  http.begin(webHookUrl);
  http.addHeader("Content-Type", "application/json");
  int status_code = http.POST((uint8_t *)json_string, strlen(json_string));
  if (status_code == 200)
  {
    Serial.printf("[POST]Send to server (URL:%s)", webHookUrl);
  }
  else
  {
    Serial.printf("[POST]failed to send to server (URL:%s)", webHookUrl);
  }
  http.end();
}

bool first_time = true;
void judge(String sendText)
{
  // Serial.println("1");
  if (isLargeAmountChange())
  {
    prevMovingAverage = 0;
    first_time = false;
    sendSlackMessage(WEBHOOK_URL, sendText);
    delay(10000);
  }
}

void setup()
{
  Serial.begin(115200);

  //// WiFi設定
  WiFi.begin(SSID, PASSWORD);
  Serial.print("connecting to WiFi.");
  int i = 0;
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(100);
    Serial.print(".");
    i++;
    if (i == 100)
    {
      i = 0;
      Serial.println("Retry!!! please check your ssid and passpword");
    }
  }
  Serial.println();
  ////

  //// センサピンの設定
  pinMode(TRG, OUTPUT);
  pinMode(ECH, INPUT);
  ////

  ////　ステータスLEDの設定
  ledcSetup(1, 12000, 8);
  ledcAttachPin(A18, 1);
  ledcWrite(1, 0);
  ////

  ////　時刻設定
  Serial.println("connected! & timeset!");
  configTime(9 * 3600L, 0, "ntp.nict.jp", "time.google.com", "ntp.jst.mfeed.ad.jp"); // NTPの設定
  getLocalTime(&timeInfo);
  before_hour = timeInfo.tm_hour;
  start_day = timeInfo.tm_mday;
  Serial.printf("now_day: %i\n", timeInfo.tm_mday);
  Serial.printf("start_day: %i\n", start_day);
  if (!(timeInfo.tm_mday == start_day))
  {
    Serial.println("起動日と現在時刻が違うためリスタート");
    ESP.restart();
  }
  ////

  //  for (int j = 0; j < 10; j++)
  // {
  //   float dis = responseEcho();
  //   Serial.print("dis: ");
  //   Serial.println((int)dis);
  //   if ((int)dis > 1200)
  //   {
  //     dis = 1200;
  //   }
  //   addData((int)dis);
  // }
  // prevMovingAverage = getMovingAverage();

  ////　起動日初回実行
  while (first_time)
  {
    judge("HaLakeがOpenしました");
  }
  ////
}

void loop()
{
  getLocalTime(&timeInfo);
  if (timeInfo.tm_hour > 18)
  {
    goto bailout;
  }
  judge("開閉しました");

bailout:
  if (1 == (timeInfo.tm_mday - start_day))
  {
    Serial.println(timeInfo.tm_mday); // debug
    delay(60000);                     // 00:01にリセット
    ESP.restart();
  }
}