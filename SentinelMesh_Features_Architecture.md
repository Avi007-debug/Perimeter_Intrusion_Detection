# 🛡️ SentinelMesh AI: System Features & Architecture

This document provides a comprehensive overview of the core features, AI methodologies, and control mechanisms integrated into the SentinelMesh AI system.

---

## 1. 🤖 AI Classification & Alert Level Methodology

The physical hardware nodes (ESP32) utilize a highly optimized, rule-based inference engine to categorize threats before they are even sent to the cloud.

### 🔊 Alert Level Distribution
The physical buzzer and visual LEDs react dynamically based on the severity and spread of the intrusion across the perimeter:

- **Level 1 (Low Threat)**: *Slow Beeping (500ms ON / 500ms OFF)*
  - Triggered by simple motion (PIR) on a single isolated node at a far distance.
- **Level 2 (Medium Threat)**: *Burst Beeping (Rapid 3x 80ms bursts)*
  - Triggered by motion across 2 nodes simultaneously.
  - Triggered by a single node if it detects vibration (SW420) OR close-range proximity (< 50cm).
- **Level 3 (High/Critical Threat)**: *Continuous Solid Alarm*
  - **Wide-Area Intrusion:** 3 or more nodes activated simultaneously.
  - **Tampering:** A single node detects both vibration AND close proximity simultaneously (e.g., someone breaking the enclosure).
  - **Severe Breach:** 2 nodes detect motion accompanied by vibration or close proximity.

### 🧠 Target Classification Scoring
The system calculates weighted scores for `HUMAN`, `ANIMAL`, and `VEHICLE` based on 7 telemetry metrics. The category with the highest score determines the classification.

1. **PIR (Motion):** Favors Human (10 pts) and Animal (8 pts).
2. **Vibration (Impact):** Heavily favors Vehicle (8 pts) and Human (6 pts).
3. **Node Count (Size/Speed):** 3+ nodes heavily favor Vehicles (15 pts); 2 nodes favor Humans (15 pts); 1 node favors Animals (15 pts).
4. **Duration:** Long durations (>6s) favor Humans; Short durations (<2s) favor Vehicles.
5. **Minimum Distance:** Close proximity (<15cm) favors Humans/Vehicles; Far distances favor Animals.
6. **Average Move Time:** Very fast movement across nodes (<3.0s) strictly flags as a Vehicle (25 pts).
7. **Scenario Overrides:**
   - *Stationary Vibration (Tampering)* directly overrides vehicle points and forces a Human classification.
   - *Fast Single-Node Disturbances* are forced into Animal classifications.

---

## 2. 📱 Telegram Bot Integration

SentinelMesh AI features a fully interactive Telegram bot (`telebot`) acting as your mobile security command center.

### Core Features:
- **Filtered Push Notifications:** To prevent spam, the bot ONLY pushes notifications to your phone if the threat level is classified as **HIGH** or **CRITICAL**.
- **Interactive Inline Controls:** Every alert message includes an inline button (`🔕 Mute Buzzer & Alerts`). Clicking it opens a sub-menu allowing you to mute the physical hardware buzzer for: `1 Min`, `10 Min`, `1 Hr`, or `1 Day`.
- **Global Commands:** You can type `/menu` in the chat at any time to open a persistent command keyboard to **Arm** or **Disarm** the entire perimeter system remotely.
- **Anti-Spam Rate Limiting:** A built-in sliding window rate limiter prevents the bot from sending more than 15 messages per minute, protecting your API key from Telegram bans.

---

## 3. 🎛️ Web Dashboard Controls

The local web dashboard (`http://localhost:5000`) provides a centralized, real-time command interface.

### Features:
- **Tabbed Interface:** Clean separation of concerns with 3 tabs: *Live Surveillance*, *Analytics*, and *System Controls*.
- **Hardware Command Proxy:** The *System Controls* tab features beautifully styled, interactive buttons to **Arm**, **Disarm**, and **Mute** the physical buzzers. 
- **MQTT Bridge:** When a dashboard button is clicked, the backend translates the HTTP POST request into an MQTT message (`perimeter/command`) which is instantly picked up by the ESP32 gateway.
- **Health Monitoring:** The dashboard continuously polls the health of the hardware. If the ESP32 gateway stops sending MQTT heartbeats for more than 15 seconds, the dashboard locks down and displays a massive red `SYSTEM OFFLINE` banner.

---

## 4. ✨ Gemini AI Security Analyst (Phase 7)

We have integrated Google's **Gemini AI** (`gemini-2.5-flash` model) directly into the dashboard to act as an automated Security Operations Center (SOC) analyst.

### How it Works:
- Located on the *Live Surveillance* tab beneath the current intrusion metrics is a **✨ Generate AI Report** button.
- When clicked, the dashboard takes the raw telemetry of the current incident (Classification, Path tracked, Distance, Nodes triggered, Threat level) and sends it to the Flask backend.
- The backend injects this data into a highly-tuned SOC system prompt and queries the `gemini-2.5-flash` model.
- **The Result:** The dashboard instantly displays a professional, 2-paragraph natural-language incident report. 

*Example Output:*
> "A vehicle-sized intrusion traversed the South to West perimeter zones within a highly rapid 2.4-second interval. The event was classified as a HIGH threat due to the activation of multiple nodes in sequence accompanied by significant vibration telemetry. No immediate human intervention is required, but the perimeter logs have been flagged."
