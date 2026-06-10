#include <WiFi.h>
#include <esp_now.h>

#define GREEN_LED 25
#define BUZZER    26
#define RED_LED   27

#define PIR_PIN   13
#define SW420_PIN 14
#define TRIG_PIN  12
#define ECHO_PIN  15

#define DIST_THRESHOLD 50
#define PATH_TIMEOUT_MS 10000
#define MAX_EVENTS      10
#define MAX_INCIDENTS   10

typedef struct
{
  uint8_t node_id;
  uint8_t pir_recent;
  uint8_t vib_recent;
  float distance;
  unsigned long trigger_time;
}
SensorData;

SensorData westData;
SensorData northData;
SensorData eastData;

struct Event
{
  uint8_t node;
  unsigned long time;
};

struct Incident
{
  unsigned long time;
  uint8_t startNode;
  uint8_t endNode;
  uint8_t maxAlert;
};

Event eventBuffer[MAX_EVENTS];
Incident incidentBuffer[MAX_INCIDENTS];
uint8_t incidentPaths[MAX_INCIDENTS][MAX_EVENTS];
uint8_t incidentPathLengths[MAX_INCIDENTS];

int eventIndex = 0;
int lastRecordedNode = -1;
unsigned long lastPathEventTime = 0;
int incidentIndex = 0;
uint8_t pathMaxAlertLevel = 0;

unsigned long westLastSeen = 0;
unsigned long northLastSeen = 0;
unsigned long eastLastSeen = 0;

unsigned long lastNode1Event = 0;
unsigned long lastNode2Event = 0;
unsigned long lastNode3Event = 0;
unsigned long lastNode4Event = 0;

unsigned long lastPirTime = 0;
unsigned long lastVibTime = 0;

unsigned long lastDetection = 0;

bool prevSouthActive = false;
bool prevWestActive  = false;
bool prevNorthActive = false;
bool prevEastActive  = false;
int currentAlertLevel = 0;

String formatMillisTime(unsigned long timeMs)
{
  unsigned long totalSeconds = timeMs / 1000;
  unsigned long hours = (totalSeconds / 3600) % 24;
  unsigned long minutes = (totalSeconds / 60) % 60;
  unsigned long seconds = totalSeconds % 60;

  char buffer[9];
  snprintf(buffer,
           sizeof(buffer),
           "%02lu:%02lu:%02lu",
           hours,
           minutes,
           seconds);

  return String(buffer);
}

float getDistance()
{
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);

  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);

  digitalWrite(TRIG_PIN, LOW);

  long duration =
      pulseIn(ECHO_PIN,
              HIGH,
              30000);

  if(duration == 0)
    return 999;

  return duration * 0.0343 / 2.0;
}

bool nodeActive(SensorData node)
{
  return (
      node.distance < DIST_THRESHOLD &&
      (
        node.pir_recent ||
        node.vib_recent
      )
  );
}

void storeIncident()
{
  if(eventIndex <= 0)
    return;

  if(incidentIndex >= MAX_INCIDENTS)
  {
    for(int i = 0; i < MAX_INCIDENTS - 1; i++)
    {
      incidentBuffer[i] = incidentBuffer[i + 1];
      incidentPathLengths[i] = incidentPathLengths[i + 1];

      for(int j = 0; j < MAX_EVENTS; j++)
      {
        incidentPaths[i][j] = incidentPaths[i + 1][j];
      }
    }

    incidentIndex = MAX_INCIDENTS - 1;
  }

  Incident &incident = incidentBuffer[incidentIndex];

  incident.time = lastPathEventTime;
  incident.startNode = eventBuffer[0].node;
  incident.endNode = eventBuffer[eventIndex - 1].node;
  incident.maxAlert = pathMaxAlertLevel;

  incidentPathLengths[incidentIndex] = eventIndex;

  for(int i = 0; i < eventIndex; i++)
  {
    incidentPaths[incidentIndex][i] = eventBuffer[i].node;
  }

  Serial.println("INCIDENT SAVED");
  Serial.print("TIME        : ");
  Serial.println(formatMillisTime(incident.time));
  Serial.print("DIRECTION   : ");
  Serial.print(nodeName(incident.startNode));
  Serial.print(" -> ");
  Serial.println(nodeName(incident.endNode));
  Serial.print("ALERT LEVEL : LEVEL ");
  Serial.println(incident.maxAlert);
  Serial.print("PATH        : ");

  for(int i = 0; i < incidentPathLengths[incidentIndex]; i++)
  {
    Serial.print(nodeName(incidentPaths[incidentIndex][i]));

    if(i < incidentPathLengths[incidentIndex] - 1)
      Serial.print(" -> ");
  }

  Serial.println();

  incidentIndex++;
}

void recordEvent(uint8_t node)
{
  if(node == lastRecordedNode)
    return;

  if(eventIndex >= MAX_EVENTS)
  {
    for(int i = 0; i < MAX_EVENTS - 1; i++)
    {
      eventBuffer[i] = eventBuffer[i + 1];
    }

    eventIndex = MAX_EVENTS - 1;
  }

  eventBuffer[eventIndex].node = node;
  eventBuffer[eventIndex].time = millis();

  lastRecordedNode = node;
  lastPathEventTime = millis();

  eventIndex++;

  Serial.print("EVENT STORED : Node ");
  Serial.println(node);
}

void OnDataRecv(
  const esp_now_recv_info *info,
  const uint8_t *incomingData,
  int len)
{
  

  SensorData incoming;

  memcpy(
      &incoming,
      incomingData,
      sizeof(incoming));



  switch(incoming.node_id)
  {
    case 2:

      westData = incoming;
      westLastSeen = millis();

      break;

    case 3:

      northData = incoming;
      northLastSeen = millis();

      break;

    case 4:

      eastData = incoming;
      eastLastSeen = millis();

      break;
  }
}

String nodeName(uint8_t node)
{
  switch(node)
  {
    case 1: return "SOUTH";
    case 2: return "WEST";
    case 3: return "NORTH";
    case 4: return "EAST";
  }

  return "UNKNOWN";
}

void setup()
{
  Serial.begin(115200);

  pinMode(GREEN_LED, OUTPUT);
  pinMode(RED_LED, OUTPUT);
  pinMode(BUZZER, OUTPUT);

  pinMode(PIR_PIN, INPUT);
  pinMode(SW420_PIN, INPUT);

  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);

  digitalWrite(GREEN_LED, HIGH);
  westData.distance = 999;
  northData.distance = 999;
  eastData.distance = 999;
  
  westData.pir_recent = 0;
  northData.pir_recent = 0;
  eastData.pir_recent = 0;
  
  westData.vib_recent = 0;
  northData.vib_recent = 0;
  eastData.vib_recent = 0;
  WiFi.mode(WIFI_STA);

  if (esp_now_init() != ESP_OK)
  {
    Serial.println("ESP-NOW Init Failed");
    return;
  }

  esp_now_register_recv_cb(OnDataRecv);

  Serial.println();
  Serial.println("================================");
  Serial.println("NODE 1 SOUTH GATEWAY READY");
  Serial.println("================================");
}

void loop()
{
  if(digitalRead(PIR_PIN))
    lastPirTime = millis();

  if(digitalRead(SW420_PIN))
    lastVibTime = millis();

  uint8_t localPir =
      (millis() - lastPirTime < 3000);

  uint8_t localVib =
      (millis() - lastVibTime < 3000);

  float localDist =
      getDistance();

  bool southActive =
      (
       localDist < DIST_THRESHOLD &&
       (
        localPir ||
        localVib
       )
      );

  bool westActive =
      nodeActive(westData);

  bool northActive =
      nodeActive(northData);

  bool eastActive =
      nodeActive(eastData);

  if(eventIndex > 0 &&
     millis() - lastPathEventTime > PATH_TIMEOUT_MS)
  {
    Serial.println();
    Serial.println("PATH RESET");
    storeIncident();

    eventIndex = 0;
    lastRecordedNode = -1;
    lastPathEventTime = 0;
    pathMaxAlertLevel = 0;
  }

if(southActive && !prevSouthActive)
{
  recordEvent(1);
}

if(westActive && !prevWestActive)
{
  recordEvent(2);
}

if(northActive && !prevNorthActive)
{
  recordEvent(3);
}

if(eastActive && !prevEastActive)
{
  recordEvent(4);
}
  bool westOnline =
      (millis() - westLastSeen < 5000);

  bool northOnline =
      (millis() - northLastSeen < 5000);

  bool eastOnline =
      (millis() - eastLastSeen < 5000);

  int activeNodes = 0;

  if(southActive) activeNodes++;
  if(westActive) activeNodes++;
  if(northActive) activeNodes++;
  if(eastActive) activeNodes++;

  int alertLevel = 0;

  if(activeNodes >= 2)
  {
    alertLevel = 3;
  }
  else if(
       (
        localDist < DIST_THRESHOLD &&
        localPir &&
        localVib
       )
       ||
       (
        westData.distance < DIST_THRESHOLD &&
        westData.pir_recent &&
        westData.vib_recent
       )
       ||
       (
        northData.distance < DIST_THRESHOLD &&
        northData.pir_recent &&
        northData.vib_recent
       )
       ||
       (
        eastData.distance < DIST_THRESHOLD &&
        eastData.pir_recent &&
        eastData.vib_recent
       )
  )
  {
    alertLevel = 2;
  }
  else if(activeNodes == 1)
  {
    alertLevel = 1;
  }

  if(alertLevel > pathMaxAlertLevel)
  {
    pathMaxAlertLevel = alertLevel;
  }

  if(alertLevel > 0)
  {
    currentAlertLevel = alertLevel;
    lastDetection = millis();
  }

  bool alertActive =
      (
       millis() - lastDetection < 5000
      );

  if(!alertActive)
  {
    digitalWrite(GREEN_LED, HIGH);
    digitalWrite(RED_LED, LOW);
    digitalWrite(BUZZER, LOW);

    currentAlertLevel = 0;
  }
  else
  {
    digitalWrite(GREEN_LED, LOW);
    digitalWrite(RED_LED, HIGH);

    if(currentAlertLevel == 1)
    {
      digitalWrite(BUZZER, HIGH);
      delay(200);

      digitalWrite(BUZZER, LOW);
      delay(800);
    }
    else if(currentAlertLevel == 2)
    {
      digitalWrite(BUZZER, HIGH);
      delay(150);

      digitalWrite(BUZZER, LOW);
      delay(150);
    }
    else if(currentAlertLevel == 3)
    {
      digitalWrite(BUZZER, HIGH);
    }
  }

  Serial.println();
Serial.println("================================");

Serial.println("NODE 1 SOUTH");

Serial.print("PIR      : ");
Serial.println(localPir);

Serial.print("VIB      : ");
Serial.println(localVib);

Serial.print("DISTANCE : ");
Serial.println(localDist);

Serial.print("ACTIVE   : ");
Serial.println(southActive);

Serial.println();

Serial.println("NODE 2 WEST");

Serial.print("PIR      : ");
Serial.println(westData.pir_recent);

Serial.print("VIB      : ");
Serial.println(westData.vib_recent);

Serial.print("DISTANCE : ");
Serial.println(westData.distance);

Serial.print("ONLINE   : ");
Serial.println(westOnline);

Serial.print("ACTIVE   : ");
Serial.println(westActive);

Serial.println();

Serial.println("NODE 3 NORTH");

Serial.print("PIR      : ");
Serial.println(northData.pir_recent);

Serial.print("VIB      : ");
Serial.println(northData.vib_recent);

Serial.print("DISTANCE : ");
Serial.println(northData.distance);

Serial.print("ONLINE   : ");
Serial.println(northOnline);

Serial.print("ACTIVE   : ");
Serial.println(northActive);

Serial.println();

Serial.println("NODE 4 EAST");

Serial.print("PIR      : ");
Serial.println(eastData.pir_recent);

Serial.print("VIB      : ");
Serial.println(eastData.vib_recent);

Serial.print("DISTANCE : ");
Serial.println(eastData.distance);

Serial.print("ONLINE   : ");
Serial.println(eastOnline);

Serial.print("ACTIVE   : ");
Serial.println(eastActive);

Serial.println();

Serial.print("ACTIVE NODES : ");
Serial.println(activeNodes);

Serial.print("ALERT LEVEL  : ");
Serial.println(alertLevel);

Serial.print("HELD LEVEL   : ");
Serial.println(currentAlertLevel);
Serial.println();
Serial.print("INCIDENTS STORED : ");
Serial.println(incidentIndex);
Serial.println();
Serial.println("RECENT PATH");

for(int i = 0; i < eventIndex; i++)
{
  Serial.print(nodeName(eventBuffer[i].node));

  if(i < eventIndex - 1)
    Serial.print(" -> ");
}

Serial.println();
Serial.println();

if(eventIndex >= 2)
{
  uint8_t from =
      eventBuffer[eventIndex - 2].node;

  uint8_t to =
      eventBuffer[eventIndex - 1].node;

  Serial.print("LAST MOVE : ");

  Serial.print(nodeName(from));
  Serial.print(" -> ");
  Serial.print(nodeName(to));

  Serial.println();
}

if(eventIndex >= 3)
{
  uint8_t startNode = eventBuffer[0].node;
  uint8_t endNode   = eventBuffer[eventIndex - 1].node;

  Serial.print("TRAVEL DIRECTION : ");

  Serial.print(nodeName(startNode));
  Serial.print(" -> ");
  Serial.println(nodeName(endNode));
}

if(incidentIndex > 0)
{
  Incident latestIncident =
      incidentBuffer[incidentIndex - 1];

  Serial.println();
  Serial.print("LAST INCIDENT : ");
  Serial.println(formatMillisTime(latestIncident.time));
}

Serial.println();

Serial.println();

Serial.println("================================");
prevSouthActive = southActive;
prevWestActive  = westActive;
prevNorthActive = northActive;
prevEastActive  = eastActive;
  delay(500);
}
