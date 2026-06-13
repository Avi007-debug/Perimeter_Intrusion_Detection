#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

#define PIR_PIN   13
#define VIB_PIN   14
#define TRIG_PIN  12
#define ECHO_PIN  15

#define DIST_THRESHOLD 50

typedef struct {
  uint8_t node_id;
  uint8_t pir_recent;
  uint8_t vib_recent;
  float distance;
  unsigned long trigger_time;
} SensorData;

SensorData data;

unsigned long lastPirTime = 0;
unsigned long lastVibTime = 0;

uint8_t gatewayMAC[] = {0x00,0x4B,0x12,0x30,0xCD,0xBC};

float getDistance() {

  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);

  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);

  digitalWrite(TRIG_PIN, LOW);

  long duration = pulseIn(ECHO_PIN, HIGH, 30000);

  if(duration == 0)
    return 999;

  return duration * 0.034 / 2.0;
}

void onDataSent(const uint8_t *mac_addr,
                esp_now_send_status_t status) {

  Serial.print("Send Status: ");

  if(status == ESP_NOW_SEND_SUCCESS)
    Serial.println("SUCCESS");
  else
    Serial.println("FAILED");
}

void setup() {

  Serial.begin(115200);

  pinMode(PIR_PIN, INPUT);
  pinMode(VIB_PIN, INPUT);

  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();

  Serial.println("Scanning for Gateway channel...");
  int32_t channel = 1;
  int n = WiFi.scanNetworks();
  for (int i = 0; i < n; i++) {
    if (WiFi.SSID(i) == "Avi's Nord 5G") {
      channel = WiFi.channel(i);
      Serial.print("Found hotspot on channel: ");
      Serial.println(channel);
      break;
    }
  }

  // Set the ESP32 to the correct channel before initializing ESP-NOW
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
  esp_wifi_set_promiscuous(false);

  Serial.print("My MAC: ");
  Serial.println(WiFi.macAddress());

  if(esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW Init Failed");
    return;
  }

  esp_now_register_send_cb(onDataSent);

  esp_now_peer_info_t peerInfo = {};

  memcpy(peerInfo.peer_addr,
         gatewayMAC,
         6);

  peerInfo.channel = channel;
  peerInfo.encrypt = false;

  if(esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("Peer Add Failed");
    return;
  }

  data.node_id = 2;
}

void loop() {

  if(digitalRead(PIR_PIN))
    lastPirTime = millis();

  if(digitalRead(VIB_PIN))
    lastVibTime = millis();

  static unsigned long lastSendTime = 0;
  if(millis() - lastSendTime >= 1000) {
    lastSendTime = millis();

    data.pir_recent = (millis() - lastPirTime < 3000);
    data.vib_recent = (millis() - lastVibTime < 3000);
    data.distance = getDistance();
    data.trigger_time = millis();

    esp_now_send(gatewayMAC, (uint8_t*)&data, sizeof(data));

    Serial.println();
    Serial.println("======================");
    Serial.println("NODE 2 WEST");
    Serial.print("PIR      : ");
    Serial.println(data.pir_recent);
    Serial.print("VIB      : ");
    Serial.println(data.vib_recent);
    Serial.print("DISTANCE : ");
    Serial.println(data.distance);
    Serial.print("TIME     : ");
    Serial.println(data.trigger_time);
    Serial.println("======================");
  }
}
