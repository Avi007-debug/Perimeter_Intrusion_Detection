#include <ESP8266WiFi.h>
extern "C" {
#include <espnow.h>
}

#define PIR_PIN    14 //D5
#define SW420_PIN  4 //D2
#define TRIG_PIN   13 //D7
#define ECHO_PIN   5 // D1

typedef struct {
  uint8_t node_id;
  uint8_t pir_recent;
  uint8_t vib_recent;
  float distance;
  unsigned long trigger_time;
} SensorData;

SensorData data;

uint8_t receiverMAC[] = {
  0x00, 0x4B, 0x12, 0x30, 0xCD, 0xBC
};

unsigned long lastPirTime = 0;
unsigned long lastVibTime = 0;

float getDistance()
{
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);

  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);

  digitalWrite(TRIG_PIN, LOW);

  long duration = pulseIn(ECHO_PIN, HIGH, 30000);

  if(duration == 0)
    return 999;

  return duration * 0.0343 / 2.0;
}

void setup()
{
  Serial.begin(115200);

  pinMode(PIR_PIN, INPUT);
  pinMode(SW420_PIN, INPUT);

  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);

  WiFi.mode(WIFI_STA);

  if (esp_now_init() != 0)
  {
    Serial.println("ESP-NOW Init Failed");
    return;
  }

  esp_now_set_self_role(ESP_NOW_ROLE_CONTROLLER);

  esp_now_add_peer(
      receiverMAC,
      ESP_NOW_ROLE_SLAVE,
      1,
      NULL,
      0);

  Serial.println("NODE 3 NORTH READY");
}

void loop()
{
  if(digitalRead(PIR_PIN))
    lastPirTime = millis();

  if(digitalRead(SW420_PIN))
    lastVibTime = millis();

  data.node_id = 3;

  data.pir_recent =
      (millis() - lastPirTime < 3000);

  data.vib_recent =
      (millis() - lastVibTime < 3000);

  data.distance = getDistance();

  data.trigger_time = millis();

  esp_now_send(
      receiverMAC,
      (uint8_t*)&data,
      sizeof(data));

  Serial.println();
  Serial.println("======================");

  Serial.println("NODE 3 NORTH");

  Serial.print("PIR      : ");
  Serial.println(data.pir_recent);

  Serial.print("VIB      : ");
  Serial.println(data.vib_recent);

  Serial.print("DISTANCE : ");
  Serial.println(data.distance);

  Serial.print("TIME     : ");
  Serial.println(data.trigger_time);

  Serial.println("======================");

  delay(1000);
}
