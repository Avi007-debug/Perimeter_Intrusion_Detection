#include <WiFi.h>
#include <esp_now.h>
#include <string.h>

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
#define CLASS_NAME_SIZE  12
#define THREAT_NAME_SIZE 12

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
  unsigned long durationMs;
  uint8_t startNode;
  uint8_t endNode;
  uint8_t nodeCount;
  uint8_t pirCount;
  uint8_t vibCount;
  uint8_t maxAlert;
  float avgMoveTime;
  float minDistance;
  char classification[CLASS_NAME_SIZE];
  char threat[THREAT_NAME_SIZE];
  uint8_t confidence;
};

struct RuleResult
{
  const char *classification;
  const char *threat;
  uint8_t confidence;
};

Event eventBuffer[MAX_EVENTS];
Incident incidentBuffer[MAX_INCIDENTS];
uint8_t incidentPaths[MAX_INCIDENTS][MAX_EVENTS];
uint8_t incidentPathLengths[MAX_INCIDENTS];

int eventIndex = 0;
int lastRecordedNode = -1;
unsigned long pathStartTime = 0;
unsigned long lastPathEventTime = 0;
int incidentIndex = 0;
uint8_t pathMaxAlertLevel = 0;
uint8_t pathPirCount = 0;
uint8_t pathVibCount = 0;
uint8_t pathNodeMask = 0;
float pathMinDistance = 999;

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
bool prevSouthPir = false;
bool prevWestPir  = false;
bool prevNorthPir = false;
bool prevEastPir  = false;
bool prevSouthVib = false;
bool prevWestVib  = false;
bool prevNorthVib = false;
bool prevEastVib  = false;
int currentAlertLevel = 0;

String nodeName(uint8_t node);

void copyText(char *target,
              const char *source,
              size_t targetSize)
{
  strncpy(target, source, targetSize - 1);
  target[targetSize - 1] = '\0';
}

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

uint8_t countPathNodes(uint8_t nodeMask)
{
  uint8_t count = 0;

  for(uint8_t i = 0; i < 4; i++)
  {
    if(nodeMask & (1 << i))
      count++;
  }

  return count;
}

String pathToString(uint8_t incidentSlot)
{
  String path = "";

  for(int i = 0; i < incidentPathLengths[incidentSlot]; i++)
  {
    path += nodeName(incidentPaths[incidentSlot][i]);

    if(i < incidentPathLengths[incidentSlot] - 1)
      path += " -> ";
  }

  return path;
}

String pathToCsvString(uint8_t incidentSlot)
{
  String path = "";

  for(int i = 0; i < incidentPathLengths[incidentSlot]; i++)
  {
    path += nodeName(incidentPaths[incidentSlot][i]);

    if(i < incidentPathLengths[incidentSlot] - 1)
      path += "->";
  }

  return path;
}

float averageMoveTime()
{
  if(eventIndex < 2)
    return 0;

  unsigned long total = 0;

  for(int i = 1; i < eventIndex; i++)
  {
    total += eventBuffer[i].time - eventBuffer[i - 1].time;
  }

  return (float)total / (eventIndex - 1) / 1000.0;
}

void printMoveIntervals()
{
  if(eventIndex < 2)
    return;

  Serial.println("MOVE INTERVALS");

  for(int i = 1; i < eventIndex; i++)
  {
    float intervalSeconds =
        (eventBuffer[i].time - eventBuffer[i - 1].time) / 1000.0;

    Serial.print(nodeName(eventBuffer[i - 1].node));
    Serial.print(" -> ");
    Serial.print(nodeName(eventBuffer[i].node));
    Serial.print(" : ");
    Serial.print(intervalSeconds);
    Serial.println(" sec");
  }
}

const char *threatFromClassification(const char *classification,
                                     uint8_t confidence,
                                     uint8_t maxAlert)
{
  if(maxAlert >= 3 && confidence >= 65)
    return "CRITICAL";

  if(strcmp(classification, "VEHICLE") == 0)
    return "HIGH";

  if(strcmp(classification, "HUMAN") == 0 && maxAlert >= 2)
    return "HIGH";

  if(strcmp(classification, "ANIMAL") == 0)
    return "MEDIUM";

  return "LOW";
}

RuleResult classifyIncident(unsigned long durationMs,
                            uint8_t nodeCount,
                            uint8_t pirCount,
                            uint8_t vibCount,
                            uint8_t maxAlert,
                            float minDistance,
                            float avgMoveTime)
{
  int humanScore = 0;
  int animalScore = 0;
  int vehicleScore = 0;

  humanScore += pirCount * 10;
  animalScore += pirCount * 7;
  vehicleScore += pirCount * 2;

  humanScore += vibCount * 3;
  animalScore += vibCount * 3;
  vehicleScore += vibCount * 12;

  if(nodeCount >= 3)
  {
    vehicleScore += 20;
    humanScore += 10;
  }
  else if(nodeCount == 2)
  {
    humanScore += 15;
  }
  else
  {
    animalScore += 10;
  }

  if(durationMs > 5000)
  {
    humanScore += 15;
  }
  else
  {
    vehicleScore += 10;
  }

  if(minDistance < 15)
  {
    vehicleScore += 10;
  }
  else if(minDistance < 30)
  {
    humanScore += 10;
  }
  else
  {
    animalScore += 10;
  }

  if(avgMoveTime > 0 && avgMoveTime < 1.0)
  {
    vehicleScore += 20;
  }
  else if(avgMoveTime > 0 && avgMoveTime < 3.0)
  {
    humanScore += 15;
  }
  else if(avgMoveTime >= 3.0)
  {
    animalScore += 10;
  }

  if(maxAlert >= 3)
  {
    vehicleScore += 8;
    humanScore += 6;
  }
  else if(maxAlert == 2)
  {
    humanScore += 6;
    animalScore += 4;
  }

  int total = humanScore + animalScore + vehicleScore;

  if(total <= 0)
    return {"UNKNOWN", "LOW", 40};

  const char *classification = "HUMAN";
  int winningScore = humanScore;

  if(animalScore > winningScore)
  {
    classification = "ANIMAL";
    winningScore = animalScore;
  }

  if(vehicleScore > winningScore)
  {
    classification = "VEHICLE";
    winningScore = vehicleScore;
  }

  uint8_t confidence = (uint8_t)((winningScore * 100) / total);
  const char *threat =
      threatFromClassification(classification,
                               confidence,
                               maxAlert);

  return {classification, threat, confidence};
}

void updatePathMetrics(uint8_t localPir,
                       uint8_t localVib,
                       float localDist,
                       bool southActive,
                       bool westActive,
                       bool northActive,
                       bool eastActive)
{
  if(eventIndex <= 0)
    return;

  bool westPir = westData.pir_recent;
  bool northPir = northData.pir_recent;
  bool eastPir = eastData.pir_recent;

  bool westVib = westData.vib_recent;
  bool northVib = northData.vib_recent;
  bool eastVib = eastData.vib_recent;

  if(localPir && !prevSouthPir) pathPirCount++;
  if(westPir && !prevWestPir) pathPirCount++;
  if(northPir && !prevNorthPir) pathPirCount++;
  if(eastPir && !prevEastPir) pathPirCount++;

  if(localVib && !prevSouthVib) pathVibCount++;
  if(westVib && !prevWestVib) pathVibCount++;
  if(northVib && !prevNorthVib) pathVibCount++;
  if(eastVib && !prevEastVib) pathVibCount++;

  prevSouthPir = localPir;
  prevWestPir = westPir;
  prevNorthPir = northPir;
  prevEastPir = eastPir;

  prevSouthVib = localVib;
  prevWestVib = westVib;
  prevNorthVib = northVib;
  prevEastVib = eastVib;

  if(southActive) pathNodeMask |= (1 << 0);
  if(westActive)  pathNodeMask |= (1 << 1);
  if(northActive) pathNodeMask |= (1 << 2);
  if(eastActive)  pathNodeMask |= (1 << 3);

  if(localDist < pathMinDistance) pathMinDistance = localDist;
  if(westData.distance < pathMinDistance) pathMinDistance = westData.distance;
  if(northData.distance < pathMinDistance) pathMinDistance = northData.distance;
  if(eastData.distance < pathMinDistance) pathMinDistance = eastData.distance;
}

void resetPathState()
{
  eventIndex = 0;
  lastRecordedNode = -1;
  pathStartTime = 0;
  lastPathEventTime = 0;
  pathMaxAlertLevel = 0;
  pathPirCount = 0;
  pathVibCount = 0;
  pathNodeMask = 0;
  pathMinDistance = 999;
  prevSouthPir = false;
  prevWestPir = false;
  prevNorthPir = false;
  prevEastPir = false;
  prevSouthVib = false;
  prevWestVib = false;
  prevNorthVib = false;
  prevEastVib = false;
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
  unsigned long durationMs = 0;

  if(pathStartTime > 0 && lastPathEventTime >= pathStartTime)
    durationMs = lastPathEventTime - pathStartTime;

  uint8_t nodeCount = countPathNodes(pathNodeMask);
  float avgMoveSeconds = averageMoveTime();

  if(nodeCount == 0)
    nodeCount = countPathNodes(1 << (eventBuffer[0].node - 1));

  RuleResult ruleResult =
      classifyIncident(durationMs,
                       nodeCount,
                       pathPirCount,
                       pathVibCount,
                       pathMaxAlertLevel,
                       pathMinDistance,
                       avgMoveSeconds);

  incident.time = lastPathEventTime;
  incident.durationMs = durationMs;
  incident.startNode = eventBuffer[0].node;
  incident.endNode = eventBuffer[eventIndex - 1].node;
  incident.nodeCount = nodeCount;
  incident.pirCount = pathPirCount;
  incident.vibCount = pathVibCount;
  incident.maxAlert = pathMaxAlertLevel;
  incident.avgMoveTime = avgMoveSeconds;
  incident.minDistance = pathMinDistance;
  incident.confidence = ruleResult.confidence;

  copyText(incident.classification,
           ruleResult.classification,
           CLASS_NAME_SIZE);

  copyText(incident.threat,
           ruleResult.threat,
           THREAT_NAME_SIZE);

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
  Serial.print("CLASS       : ");
  Serial.print(incident.classification);
  Serial.print(" (");
  Serial.print(incident.confidence);
  Serial.println("%)");
  Serial.print("THREAT      : ");
  Serial.println(incident.threat);
  Serial.print("DURATION    : ");
  Serial.print(incident.durationMs / 1000.0);
  Serial.println(" sec");
  Serial.print("AVG MOVE TIME : ");
  Serial.print(incident.avgMoveTime);
  Serial.println(" sec");
  Serial.print("NODE COUNT  : ");
  Serial.println(incident.nodeCount);
  Serial.print("MIN DIST    : ");
  Serial.print(incident.minDistance);
  Serial.println(" cm");
  Serial.print("PATH        : ");

  for(int i = 0; i < incidentPathLengths[incidentIndex]; i++)
  {
    Serial.print(nodeName(incidentPaths[incidentIndex][i]));

    if(i < incidentPathLengths[incidentIndex] - 1)
      Serial.print(" -> ");
  }

  Serial.println();
  printMoveIntervals();

  Serial.print("CSV,");
  Serial.print(formatMillisTime(incident.time));
  Serial.print(",");
  Serial.print(pathToCsvString(incidentIndex));
  Serial.print(",");
  Serial.print(incident.nodeCount);
  Serial.print(",");
  Serial.print(incident.pirCount);
  Serial.print(",");
  Serial.print(incident.vibCount);
  Serial.print(",");
  Serial.print(incident.durationMs / 1000.0);
  Serial.print(",");
  Serial.print(incident.avgMoveTime);
  Serial.print(",");
  Serial.print(incident.minDistance);
  Serial.print(",");
  Serial.println(incident.classification);

  Serial.print("JSON        : {\"time\":\"");
  Serial.print(formatMillisTime(incident.time));
  Serial.print("\",\"direction\":\"");
  Serial.print(nodeName(incident.startNode));
  Serial.print(" -> ");
  Serial.print(nodeName(incident.endNode));
  Serial.print("\",\"path\":\"");
  Serial.print(pathToString(incidentIndex));
  Serial.print("\",\"classification\":\"");
  Serial.print(incident.classification);
  Serial.print("\",\"threat\":\"");
  Serial.print(incident.threat);
  Serial.print("\",\"confidence\":");
  Serial.print(incident.confidence);
  Serial.print(",\"alertLevel\":");
  Serial.print(incident.maxAlert);
  Serial.print(",\"durationSec\":");
  Serial.print(incident.durationMs / 1000.0);
  Serial.print(",\"avgMoveTime\":");
  Serial.print(incident.avgMoveTime);
  Serial.print(",\"nodeCount\":");
  Serial.print(incident.nodeCount);
  Serial.print(",\"pirCount\":");
  Serial.print(incident.pirCount);
  Serial.print(",\"vibCount\":");
  Serial.print(incident.vibCount);
  Serial.print(",\"minDistance\":");
  Serial.print(incident.minDistance);
  Serial.println("}");

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

  if(eventIndex == 0)
    pathStartTime = millis();

  pathNodeMask |= (1 << (node - 1));
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
  Serial.println("CSV,Time,Path,NodeCount,PirCount,VibCount,Duration,AvgMoveTime,MinDistance,Classification");
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

    resetPathState();
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

  updatePathMetrics(localPir,
                    localVib,
                    localDist,
                    southActive,
                    westActive,
                    northActive,
                    eastActive);

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

if(eventIndex > 0)
{
  RuleResult liveRule =
      classifyIncident(millis() - pathStartTime,
                       countPathNodes(pathNodeMask),
                       pathPirCount,
                       pathVibCount,
                       pathMaxAlertLevel,
                       pathMinDistance,
                       averageMoveTime());

  Serial.print("LIVE CLASS  : ");
  Serial.print(liveRule.classification);
  Serial.print(" (");
  Serial.print(liveRule.confidence);
  Serial.println("%)");

  Serial.print("LIVE THREAT : ");
  Serial.println(liveRule.threat);
}

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

printMoveIntervals();

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
