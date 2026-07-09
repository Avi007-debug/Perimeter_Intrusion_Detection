# SentinelMesh AI Startup Guide

Welcome to the SentinelMesh AI setup guide. Follow these steps to properly flash your hardware, configure your environment, and launch the Smart Perimeter Intrusion System.

> [!IMPORTANT]
> **Prerequisites:** You must have a working MQTT Broker (like Eclipse Mosquitto) installed on your host machine. You will also need Python installed for the backend, and the Arduino IDE for flashing the microcontrollers.

---

## 1. Hardware Preparation & Flashing

The system requires one Gateway node and multiple sensor nodes.

1. **Sensor Nodes (ESP8266):**
   - Open the `ESP8266/ESP8266.ino` sketch in your Arduino IDE.
   - Connect your ESP8266 boards via USB.
   - Compile and upload the code to each of your sensor nodes.
   - *Hardware Wiring:* Ensure PIR, SW420 Vibration, and Ultrasonic sensors are correctly wired to the defined pins in the sketch.

2. **Gateway Node (ESP32):**
   - Open the `Esp32/Esp32.ino` sketch.
   - **Crucial v2.1 Step:** Ensure you are using the latest code which contains the unmute buzzer fix (`>= buzzerMutedUntil`).
   - Compile and upload to your ESP32 board. 
   - *Note:* The Gateway (South Node) acts as the bridge to the MQTT broker, so ensure your WiFi credentials within the sketch (if applicable) match your local network.

---

## 2. Software & Environment Setup

Set up the Python backend to process telemetry and serve the dashboard.

1. **Virtual Environment:**
   Open a terminal in the root of the project directory and run:
   ```bash
   python -m venv venv
   ```

2. **Activate the Environment:**
   - **Windows:** `venv\Scripts\Activate.ps1` (or `.bat`)
   - **Linux/Mac:** `source venv/bin/activate`

3. **Install Dependencies:**
   ```bash
   pip install -r backend/requirements.txt
   ```

4. **Configuration (`.env` file):**
   Create a `.env` file in the root directory (you can copy `.env.example` if it exists).
   ```env
   # Required Configuration
   MQTT_BROKER=localhost
   MQTT_PORT=1883

   # Optional: Cloud Database Sync
   SUPABASE_URL=your_supabase_url
   SUPABASE_KEY=your_supabase_key

   # Optional: Telegram Notifications
   TELEGRAM_BOT_TOKEN=your_telegram_bot_token
   TELEGRAM_CHAT_ID=your_telegram_chat_id

   # Optional: AI Incident Reports
   GEMINI_API_KEY=your_gemini_api_key
   ```

---

## 3. External Services Initialization (Optional)

- **MQTT Broker:** If Mosquitto isn't running as a background service, start it manually:
  ```bash
  mosquitto -c mosquitto.conf
  ```
- **Supabase:** If using cloud sync, open your Supabase SQL Editor and execute the contents of `backend/supabase_incidents.sql` to initialize the database schema.
- **Telegram:** Start a chat with your Telegram Bot and send `/start` or `/menu` to initialize the control panel.

---

## 4. Running the System

With the hardware powered on and the environment configured, you are ready to start.

1. Ensure your terminal has the virtual environment activated.
2. Launch the Flask Backend Server:
   ```bash
   python backend/app.py
   ```
3. Once the server is running, open your web browser and navigate to:
   ```
   http://localhost:5000
   ```

---

## 5. System Operation & Verification

> [!TIP]
> **Initial Testing:** When you first boot the system, it is recommended to test the "Arm/Disarm" functionality before testing the physical alarms.

- **Check Connectivity:** Look at the "Live Surveillance" tab. Your nodes should appear online and "Idle" (green). If they show "Offline" (gray), verify your MQTT broker and ESP32 connection.
- **Arm System:** Use the Dashboard Controls tab or Telegram (`/arm`) to activate monitoring.
- **Trigger an Event:** Walk past one of the sensor nodes to simulate an intrusion. You should see the dashboard update instantly with the classification, path, and threat level.
- **Mute Test:** Use the Mute button on the dashboard to test the v2.1 buzzer synchronization. The physical buzzer should silence, and clicking "Unmute" should immediately restore it.
