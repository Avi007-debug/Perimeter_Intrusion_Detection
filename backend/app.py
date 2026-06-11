"""
SentinelMesh AI — Flask MQTT Bridge & REST API
"""

import csv
import json
import os
import threading
from datetime import datetime
from collections import deque

from flask import Flask, jsonify, send_from_directory
from flask_cors import CORS
import paho.mqtt.client as mqtt

# ── Config ────────────────────────────────────────────────────────────────────
MQTT_BROKER      = "localhost"
MQTT_PORT        = 1883
INCIDENT_TOPIC   = "perimeter/incident"
STATUS_TOPIC     = "perimeter/status"
HISTORY_MAXLEN   = 100
CSV_FILE         = os.path.join(os.path.dirname(__file__), "incidents.csv")

# ── State (thread-safe via lock) ───────────────────────────────────────────────
lock             = threading.Lock()
latest_incident  = {}
latest_status    = {"south": False, "west": False, "north": False, "east": False}
incident_history = deque(maxlen=HISTORY_MAXLEN)

# ── CSV initialisation ─────────────────────────────────────────────────────────
CSV_HEADERS = ["time", "classification", "threat", "confidence",
               "alertLevel", "path", "durationSec", "avgMoveTime",
               "nodeCount", "pirCount", "vibCount", "minDistance"]

if not os.path.exists(CSV_FILE):
    with open(CSV_FILE, "w", newline="") as f:
        csv.writer(f).writerow(CSV_HEADERS)


def log_incident_csv(data: dict):
    """Append one incident row to incidents.csv (ML dataset)."""
    row = [data.get(h, "") for h in CSV_HEADERS]
    with open(CSV_FILE, "a", newline="") as f:
        csv.writer(f).writerow(row)


# ── MQTT callbacks ─────────────────────────────────────────────────────────────
def on_connect(client, userdata, flags, rc):
    if rc == 0:
        print(f"[MQTT] Connected to broker {MQTT_BROKER}:{MQTT_PORT}")
        client.subscribe("perimeter/#")
    else:
        print(f"[MQTT] Connection failed, rc={rc}")


def on_message(client, userdata, msg):
    global latest_incident, latest_status

    payload = msg.payload.decode("utf-8", errors="ignore").strip()
    print(f"[MQTT] {msg.topic}: {payload}")

    try:
        data = json.loads(payload)
    except json.JSONDecodeError:
        print("[MQTT] Bad JSON, ignoring.")
        return

    if msg.topic == INCIDENT_TOPIC:
        data.setdefault("receivedAt", datetime.now().isoformat(timespec="seconds"))
        with lock:
            latest_incident = data
            incident_history.appendleft(data)      # newest first
        log_incident_csv(data)

    elif msg.topic == STATUS_TOPIC:
        with lock:
            latest_status = data


# ── MQTT client (background thread) ───────────────────────────────────────────
mqtt_client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION1)
mqtt_client.on_connect = on_connect
mqtt_client.on_message = on_message
mqtt_client.reconnect_delay_set(min_delay=1, max_delay=10)


def start_mqtt():
    try:
        mqtt_client.connect(MQTT_BROKER, MQTT_PORT, keepalive=60)
        mqtt_client.loop_forever()
    except Exception as exc:
        print(f"[MQTT] Could not connect to {MQTT_BROKER}:{MQTT_PORT} — {exc}")
        print("[MQTT] Retrying in background…")


mqtt_thread = threading.Thread(target=start_mqtt, daemon=True)
mqtt_thread.start()


# ── Flask app ─────────────────────────────────────────────────────────────────
# ../frontend/ is served as the static root so every file in it is reachable.
FRONTEND_DIR = os.path.normpath(
    os.path.join(os.path.dirname(__file__), "..", "frontend")
)

app = Flask(__name__, static_folder=FRONTEND_DIR, static_url_path="")
CORS(app)   # allow browser fetch() from any origin during development


@app.route("/")
def dashboard():
    """Serve the SentinelMesh AI dashboard."""
    return send_from_directory(FRONTEND_DIR, "index.html")


# ── Health / diagnostics ───────────────────────────────────────────────────────
@app.route("/api/health")
def api_health():
    """Quick ping to confirm the server is reachable."""
    return jsonify({
        "status": "ok",
        "mqtt_connected": mqtt_client.is_connected(),
        "incidents_stored": len(incident_history),
    })


# ── Data API ───────────────────────────────────────────────────────────────────
@app.route("/api/status")
def api_status():
    """Live node active/idle status."""
    with lock:
        return jsonify(latest_status)


@app.route("/api/current")
def api_current():
    """Latest classified incident."""
    with lock:
        return jsonify(latest_incident)


@app.route("/api/history")
def api_history():
    """Last 100 incidents, newest first."""
    with lock:
        return jsonify(list(incident_history))


if __name__ == "__main__":
    print(f"[SentinelMesh AI] Serving dashboard from: {FRONTEND_DIR}")
    print("[SentinelMesh AI] Dashboard → http://localhost:5000")
    app.run(host="0.0.0.0", port=5000, debug=False)
