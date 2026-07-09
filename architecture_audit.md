# SentinelMesh AI Architecture & Technical Audit

## 1. System Architecture Overview

SentinelMesh AI leverages a distributed, edge-to-cloud architecture designed for real-time perimeter intrusion detection. 

The architecture is divided into three primary layers:
1. **Edge/Sensor Layer:** A distributed mesh of physical nodes placed along the perimeter (South, West, North, East).
2. **Gateway & Processing Layer:** A central ESP32 node that acts as an aggregator and local intelligence hub, communicating with a local backend server.
3. **Application & Cloud Layer:** A Python Flask backend that serves a local web dashboard, interfaces with cloud databases (Supabase), provides an AI integration layer (Gemini), and sends remote notifications (Telegram).

## 2. Hardware & Sensor Components

The system relies on a multi-sensor fusion approach to gather precise telemetry.

### Microcontrollers
- **ESP32 (Gateway / South Node):** Acts as the primary aggregator. It receives telemetry from the sensor nodes, runs the local classification engine, triggers physical alarms, and bridges the hardware to the IT network via WiFi/MQTT.
- **ESP8266 (Sensor Nodes):** Used for the distributed nodes across the perimeter. These capture sensor data and transmit it ultra-fast to the Gateway.

### Sensors (Per Node)
- **PIR (Passive Infrared):** Detects general motion and thermal signatures.
- **SW420 Vibration Sensor:** Detects physical impact, tampering, or heavy footsteps.
- **Ultrasonic Distance Sensor:** Measures the proximity/distance of the intruding object.

## 3. Communication Protocols

To achieve ultra-low latency while maintaining robust backend integrations, the system employs a mix of protocols:

- **ESP-NOW:** Used for microcontroller-to-microcontroller communication (ESP8266 -> ESP32). ESP-NOW is a connectionless Wi-Fi communication protocol that provides sub-millisecond latency, crucial for instantaneous intrusion tracking across the mesh.
- **MQTT (Message Queuing Telemetry Transport):** Used between the ESP32 Gateway and the Python Backend. It runs on port 1883 and provides a lightweight publish/subscribe model for telemetry updates and remote commands (Arm/Disarm/Mute).
- **HTTP/REST:** Used by the Web Dashboard to fetch historical data, analytics, and trigger actions on the Flask backend.
- **HTTPS:** Used by the backend to securely communicate with external APIs (Telegram Bot API for alerts and Gemini API for AI report generation).

## 4. Technical Implementation Audit

### 🧠 Edge AI Classification Engine
Instead of relying on cloud processing, the ESP32 Gateway utilizes a highly optimized, rule-based inference engine. It calculates weighted scores across 7 telemetry metrics to classify threats as **HUMAN**, **ANIMAL**, or **VEHICLE**:
1. **PIR (Motion):** Favors Human (10 pts) / Animal (8 pts).
2. **Vibration (Impact):** Heavily favors Vehicle (8 pts) / Human (6 pts).
3. **Node Count:** Tracks intrusion size and spread (e.g., 3+ nodes heavily favor Vehicles).
4. **Duration:** Long durations (>6s) favor Humans; short durations (<2s) favor Vehicles.
5. **Minimum Distance:** Close proximity (<15cm) favors Humans/Vehicles.
6. **Average Move Time:** Rapid movement across nodes strongly flags as a Vehicle (25 pts).
7. **Scenario Overrides:** Specific patterns (e.g., stationary vibration) override scores to flag tampering (forces Human classification).

### 🚨 Alert Level & Control Logic
- **Threat Levels:** Level 1 (Low - slow beep), Level 2 (Medium - burst beep), Level 3 (High/Critical - solid alarm). Based on node spread and proximity/vibration combo.
- **Hardware Control:** The system can be armed/disarmed or the physical buzzer can be muted for specific durations (1min, 10min, 1hr, 1day).
- **State Synchronization:** In version 2.1, the logic relies on timestamp comparisons (`if(millis() >= buzzerMutedUntil)`) on the ESP32, synced via MQTT, ensuring the Dashboard, Telegram, and Hardware are perfectly aligned.

### 🌐 Backend & Data Persistence
- **Local Persistence:** Incidents are logged locally to `backend/incidents.csv` ensuring offline capability.
- **Cloud Synchronization:** An optional Supabase integration allows for cloud synchronization and advanced SQL analytics (e.g., querying frequent paths, daily summaries).
- **Health Polling:** The dashboard actively monitors MQTT heartbeats. If the ESP32 fails to check in for >15 seconds, a "SYSTEM OFFLINE" lockdown is triggered visually.

### ✨ AI Integration (Gemini)
The system leverages Google's `gemini-2.5-flash` model. The backend constructs a structured prompt using raw telemetry (Classification, Path, Distance, Nodes, Threat Level) to dynamically generate professional, SOC-style natural language incident reports on demand.
