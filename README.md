# SentinelMesh AI — Smart Perimeter Intrusion System

SentinelMesh AI is a real-time smart perimeter intrusion detection system powered by an ESP-NOW mesh network and AI classification. It uses ESP32 and ESP8266 microcontrollers with PIR and vibration sensors to detect, classify, and track perimeter breaches in real time.

## Project Structure

- `frontend/`: Web dashboard (HTML/JS/CSS) visualizing real-time alerts, cloud analytics, and perimeter maps.
- `backend/`: Flask-based MQTT Bridge & REST API. It listens to MQTT topics, logs data to a local CSV and a Supabase database, and serves the dashboard.
- `ESP32/` & `ESP8266/`: Microcontroller code for the sensor nodes and the gateway.

## 🆕 Recent Updates
- **Dashboard CSV Export:** Added a direct "Export CSV" button for Phase 6 Machine Learning dataset collection.
- **Asynchronous Storage:** Moved Supabase uploads to a background thread to prevent the MQTT client from blocking and dropping incident packets.
- **Improved Alert Level Distribution:** Completely rewrote the alert level logic. Normal motion scales slowly (Level 1 -> 2), but severe tampering (Vibration + close proximity) or wide-area intrusions (3+ nodes) instantly trigger the Level 3 solid alarm.
- **Responsive Vehicle Tracking:** Relaxed the `avgMoveTime` for Vehicle classification to `3.0s` and reduced `PATH_TIMEOUT_MS` to `4000ms` for snappier real-time dashboard updates during live demos.
- **Interactive Telegram Bot:** Added `pyTelegramBotAPI` integration. Alerts now include inline buttons to temporarily mute the physical hardware buzzer.
- **Telegram Rate Limiting:** Built-in rate limiter blocks bursts of more than 15 alerts per minute, protecting against API bans.

---

## 🚀 Setup & Installation

### 1. Hardware & MQTT
- Flash the sensor nodes and the gateway with the respective code from the `ESP32/` and `ESP8266/` folders.
- Ensure your MQTT broker (e.g., Mosquitto) is running.

### 2. Backend & Flask API
```bash
cd backend
python -m venv venv
source venv/bin/activate  # On Windows: venv\Scripts\activate
pip install -r requirements.txt
```

Create a `.env` file in the root or `backend/` directory:
```env
MQTT_BROKER=localhost
MQTT_PORT=1883
SUPABASE_URL=your_supabase_project_url
SUPABASE_KEY=your_supabase_service_role_key
TELEGRAM_BOT_TOKEN=your_telegram_bot_token
TELEGRAM_CHAT_ID=your_telegram_chat_id
```

Run the server:
```bash
python app.py
```
The dashboard will be available at `http://localhost:5000`.

---

## 🗄️ Supabase Database Setup

To enable cloud analytics and historical tracking, run the SQL script located at `backend/supabase_incidents.sql` in your Supabase SQL Editor. This sets up the `incidents` table and various analytical views for the dashboard.

### Advanced SQL Analytics Queries (Further Queries to run in Supabase)

If you want to extract deeper insights from your collected data, you can run these additional advanced queries in your Supabase SQL Editor:

**1. Most Frequent Intrusion Paths (Path Analysis)**
Identify which routes intruders are taking the most.
```sql
SELECT path, count(*) as frequency
FROM incidents
WHERE path IS NOT NULL AND path != ''
GROUP BY path
ORDER BY frequency DESC
LIMIT 5;
```

**2. Daily Incident Summary**
Get a breakdown of total incidents and critical threats per day.
```sql
SELECT 
  date_trunc('day', created_at) as day,
  count(*) as total_incidents,
  count(*) filter (where upper(threat) = 'CRITICAL' or alert_level >= 3) as critical_incidents
FROM incidents
GROUP BY day
ORDER BY day DESC;
```

**3. Average Intrusion Duration by Classification**
Find out which type of intrusion (Human, Animal, Vehicle) lasts the longest in the perimeter.
```sql
SELECT 
  upper(classification) as classification,
  round(avg(duration_sec)::numeric, 2) as avg_duration_seconds
FROM incidents
WHERE duration_sec IS NOT NULL
GROUP BY upper(classification)
ORDER BY avg_duration_seconds DESC;
```

**4. Sensor Activity Correlation (PIR vs. Vibration)**
Analyze how often both PIR and Vibration sensors are triggered simultaneously for High/Critical threats.
```sql
SELECT 
  count(*) as total_high_threats,
  count(*) filter (where pir_count > 0 and vib_count > 0) as multi_sensor_triggers,
  round((count(*) filter (where pir_count > 0 and vib_count > 0)::numeric / count(*)) * 100, 2) as multi_sensor_percentage
FROM incidents
WHERE upper(threat) IN ('HIGH', 'CRITICAL');
```

---

## 🚨 Telegram Alerts Setup

The system can push real-time alerts to your phone for **HIGH** and **CRITICAL** threats.

1. **Create a Bot**: Message `@BotFather` on Telegram, send `/newbot`, and follow the steps to get your `TELEGRAM_BOT_TOKEN`.
2. **Get Chat ID**: Send a message to your new bot. Then visit `https://api.telegram.org/bot<YOUR_BOT_TOKEN>/getUpdates` in your browser. Look for `"chat":{"id": 123456789}`.
3. **Update `.env`**: Add `TELEGRAM_BOT_TOKEN` and `TELEGRAM_CHAT_ID` to your backend `.env` file.
4. **Restart**: Restart the Flask server.

---

## 🗺️ Roadmap & Future Phases

We have successfully implemented Phases 1 through 4 (Supabase Storage, Analytics Dashboard, Telegram Alerts, and Historical Search).

**Phase 5: Dataset Collection**
- Run the system and collect 200–500 incidents in real-world scenarios (walking, running, multiple people, animal-sized movement, vehicle simulation).
- Use the **Export CSV** button on the dashboard to download this dataset.

**Phase 6: Machine Learning Integration**
- Train a Random Forest (or similar) model using the collected dataset (`incidents.csv`).
- Inputs: `nodeCount`, `pirCount`, `vibCount`, `durationSec`, `avgMoveTime`, `minDistance`, `alertLevel`.
- Output: Classified label (Human, Animal, Vehicle).
- Compare the ML accuracy vs. the current Rule Engine accuracy.

**Phase 7: Gemini AI Integration (Generative Reports)**
- Pass the classified ML incident data to the Gemini API to generate human-readable incident summaries.
- e.g., *"A human-sized intrusion traversed three perimeter zones within a short time interval. The event was classified as HIGH threat because multiple nodes were activated in sequence."*
- Useful for daily security reports and detailed incident logging.
