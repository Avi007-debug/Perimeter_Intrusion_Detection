"""
SentinelMesh AI — Flask MQTT Bridge & REST API
"""

import csv
import json
import os
import threading
import time
from datetime import datetime, timedelta, timezone
from collections import deque

from flask import Flask, jsonify, request, send_file, send_from_directory
from flask_cors import CORS
import paho.mqtt.client as mqtt
from dotenv import load_dotenv
from supabase import create_client

load_dotenv(os.path.join(os.path.dirname(__file__), "..", ".env"))
load_dotenv(os.path.join(os.path.dirname(__file__), ".env"))

# ── Config ────────────────────────────────────────────────────────────────────
MQTT_BROKER      = os.getenv("MQTT_BROKER", "localhost")
MQTT_PORT        = int(os.getenv("MQTT_PORT", "1883"))
INCIDENT_TOPIC   = "perimeter/incident"
STATUS_TOPIC     = "perimeter/status"
HISTORY_MAXLEN   = int(os.getenv("HISTORY_MAXLEN", "100"))
CSV_FILE         = os.path.join(os.path.dirname(__file__), "incidents.csv")
SUPABASE_URL     = os.getenv("SUPABASE_URL", "").strip()
SUPABASE_KEY     = (
    os.getenv("SUPABASE_SERVICE_ROLE_KEY", "").strip()
    or os.getenv("SUPABASE_KEY", "").strip()
)
SUPABASE_TABLE   = os.getenv("SUPABASE_INCIDENTS_TABLE", "incidents")
SUPABASE_ENABLED = bool(SUPABASE_URL and SUPABASE_KEY)
supabase_client  = create_client(SUPABASE_URL, SUPABASE_KEY) if SUPABASE_ENABLED else None

# ── State (thread-safe via lock) ───────────────────────────────────────────────
lock             = threading.Lock()
latest_incident  = {}
latest_status    = {"south": False, "west": False, "north": False, "east": False}
incident_history = deque(maxlen=HISTORY_MAXLEN)
storage_health   = {
    "csv_ok": True,
    "supabase_enabled": SUPABASE_ENABLED,
    "supabase_ok": False if SUPABASE_ENABLED else None,
    "last_csv_latency_ms": None,
    "last_supabase_latency_ms": None,
    "last_total_ingest_latency_ms": None,
    "last_error": None,
}

# ── CSV initialisation ─────────────────────────────────────────────────────────
CSV_HEADERS = ["time", "classification", "threat", "confidence",
               "alertLevel", "path", "durationSec", "avgMoveTime",
               "nodeCount", "pirCount", "vibCount", "minDistance"]

if not os.path.exists(CSV_FILE):
    with open(CSV_FILE, "w", newline="") as f:
        csv.writer(f).writerow(CSV_HEADERS)


def log_incident_csv(data: dict):
    """Append one incident row to incidents.csv (ML dataset)."""
    started = time.perf_counter()
    row = [data.get(h, "") for h in CSV_HEADERS]
    with open(CSV_FILE, "a", newline="") as f:
        csv.writer(f).writerow(row)
    return round((time.perf_counter() - started) * 1000, 2)


def to_int(value):
    if value in (None, ""):
        return None
    try:
        return int(value)
    except (TypeError, ValueError):
        return None


def to_float(value):
    if value in (None, ""):
        return None
    try:
        return float(value)
    except (TypeError, ValueError):
        return None


def incident_to_supabase_row(data: dict):
    """Map ESP/MQTT camelCase payload fields to Supabase snake_case columns."""
    return {
        "classification": data.get("classification"),
        "confidence": to_int(data.get("confidence")),
        "threat": data.get("threat"),
        "alert_level": to_int(data.get("alertLevel")),
        "path": data.get("path"),
        "duration_sec": to_float(data.get("durationSec")),
        "avg_move_time": to_float(data.get("avgMoveTime")),
        "node_count": to_int(data.get("nodeCount")),
        "pir_count": to_int(data.get("pirCount")),
        "vib_count": to_int(data.get("vibCount")),
        "min_distance": to_float(data.get("minDistance")),
    }


def supabase_row_to_api(row: dict):
    """Map Supabase snake_case rows back to the dashboard's existing camelCase shape."""
    if not row:
        return {}
    created_at = row.get("created_at")
    display_time = created_at
    if created_at:
        try:
            display_time = datetime.fromisoformat(created_at.replace("Z", "+00:00")).astimezone().strftime("%H:%M:%S")
        except ValueError:
            display_time = created_at

    return {
        "id": row.get("id"),
        "createdAt": created_at,
        "time": display_time,
        "classification": row.get("classification"),
        "confidence": row.get("confidence"),
        "threat": row.get("threat"),
        "alertLevel": row.get("alert_level"),
        "path": row.get("path"),
        "durationSec": row.get("duration_sec"),
        "avgMoveTime": row.get("avg_move_time"),
        "nodeCount": row.get("node_count"),
        "pirCount": row.get("pir_count"),
        "vibCount": row.get("vib_count"),
        "minDistance": row.get("min_distance"),
    }


def summarize_incidents(items):
    stats = {
        "kpis": {
            "totalIncidents": len(items),
            "humans": 0,
            "animals": 0,
            "vehicles": 0,
            "criticalAlerts": 0,
        },
        "classificationDistribution": {"HUMAN": 0, "ANIMAL": 0, "VEHICLE": 0, "UNKNOWN": 0},
        "threatDistribution": {"LOW": 0, "MEDIUM": 0, "HIGH": 0, "CRITICAL": 0, "NONE": 0},
        "nodeActivity": {"SOUTH": 0, "WEST": 0, "NORTH": 0, "EAST": 0},
        "incidentsPerHour": {},
    }

    for inc in items:
        classification = (inc.get("classification") or "UNKNOWN").upper()
        threat = (inc.get("threat") or "NONE").upper()
        alert_level = to_int(inc.get("alertLevel")) or 0

        if classification not in stats["classificationDistribution"]:
            classification = "UNKNOWN"
        if threat not in stats["threatDistribution"]:
            threat = "NONE"

        stats["classificationDistribution"][classification] += 1
        stats["threatDistribution"][threat] += 1

        if classification == "HUMAN":
            stats["kpis"]["humans"] += 1
        elif classification == "ANIMAL":
            stats["kpis"]["animals"] += 1
        elif classification == "VEHICLE":
            stats["kpis"]["vehicles"] += 1
        if threat == "CRITICAL" or alert_level >= 3:
            stats["kpis"]["criticalAlerts"] += 1

        path = (inc.get("path") or "").replace("->", ",").replace("→", ",")
        for node in path.split(","):
            node = node.strip().upper()
            if node in stats["nodeActivity"]:
                stats["nodeActivity"][node] += 1

        created_at = inc.get("createdAt") or inc.get("receivedAt")
        if created_at:
            hour = created_at[:13] + ":00"
            stats["incidentsPerHour"][hour] = stats["incidentsPerHour"].get(hour, 0) + 1

    return stats


def log_incident_supabase(data: dict):
    if not supabase_client:
        return None, None

    started = time.perf_counter()
    result = (
        supabase_client
        .table(SUPABASE_TABLE)
        .insert(incident_to_supabase_row(data))
        .execute()
    )
    latency_ms = round((time.perf_counter() - started) * 1000, 2)
    row = result.data[0] if result.data else None
    return row, latency_ms


def query_supabase_incidents(limit=100, classification=None, threat=None, alert_level=None, hours=None):
    if not supabase_client:
        return None

    query = supabase_client.table(SUPABASE_TABLE).select("*").order("created_at", desc=True).limit(limit)
    if classification:
        query = query.ilike("classification", classification)
    if threat:
        query = query.ilike("threat", threat)
    if alert_level is not None:
        query = query.gte("alert_level", alert_level)
    if hours:
        since = datetime.now(timezone.utc) - timedelta(hours=hours)
        query = query.gte("created_at", since.isoformat())
    try:
        return query.execute().data
    except Exception as exc:
        print(f"[Supabase] Query failed: {exc}")
        return None


# ── MQTT callbacks ─────────────────────────────────────────────────────────────
def on_connect(client, userdata, flags, reason_code, properties=None):
    if reason_code == 0:
        print(f"[MQTT] Connected to broker {MQTT_BROKER}:{MQTT_PORT}")
        client.subscribe("perimeter/#")
    else:
        print(f"[MQTT] Connection failed, rc={reason_code}")


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
        ingest_started = time.perf_counter()
        data.setdefault("receivedAt", datetime.now().astimezone().isoformat(timespec="seconds"))
        csv_latency_ms = None
        supabase_latency_ms = None
        supabase_status = "disabled"

        try:
            csv_latency_ms = log_incident_csv(data)
            storage_health["csv_ok"] = True
            storage_health["last_csv_latency_ms"] = csv_latency_ms
        except Exception as exc:
            storage_health["csv_ok"] = False
            storage_health["last_error"] = f"CSV: {exc}"
            print(f"[CSV] Write failed: {exc}")

        try:
            cloud_row, supabase_latency_ms = log_incident_supabase(data)
            if cloud_row:
                data["id"] = cloud_row.get("id")
                data["createdAt"] = cloud_row.get("created_at")
            supabase_status = "ok" if supabase_client else "disabled"
            storage_health["supabase_ok"] = True if supabase_client else None
            storage_health["last_supabase_latency_ms"] = supabase_latency_ms
        except Exception as exc:
            supabase_status = "error"
            storage_health["supabase_ok"] = False
            storage_health["last_error"] = f"Supabase: {exc}"
            print(f"[Supabase] Insert failed: {exc}")

        total_latency_ms = round((time.perf_counter() - ingest_started) * 1000, 2)
        data["csvLatencyMs"] = csv_latency_ms
        data["supabaseLatencyMs"] = supabase_latency_ms
        data["ingestLatencyMs"] = total_latency_ms
        data["storageStatus"] = supabase_status
        storage_health["last_total_ingest_latency_ms"] = total_latency_ms

        with lock:
            latest_incident = data
            incident_history.appendleft(data)      # newest first

    elif msg.topic == STATUS_TOPIC:
        with lock:
            latest_status = data


# ── MQTT client (background thread) ───────────────────────────────────────────
mqtt_client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)
mqtt_client.on_connect = on_connect
mqtt_client.on_message = on_message
mqtt_client.reconnect_delay_set(min_delay=1, max_delay=10)


def start_mqtt():
    while True:
        try:
            mqtt_client.connect(MQTT_BROKER, MQTT_PORT, keepalive=60)
            mqtt_client.loop_forever()
        except Exception as exc:
            print(f"[MQTT] Could not connect to {MQTT_BROKER}:{MQTT_PORT} — {exc}")
            print("[MQTT] Retrying in 5 seconds…")
            time.sleep(5)


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
        "supabase_enabled": SUPABASE_ENABLED,
        "supabase_ok": storage_health["supabase_ok"],
        "csv_ok": storage_health["csv_ok"],
        "latency": {
            "csv_ms": storage_health["last_csv_latency_ms"],
            "supabase_ms": storage_health["last_supabase_latency_ms"],
            "ingest_ms": storage_health["last_total_ingest_latency_ms"],
        },
        "last_error": storage_health["last_error"],
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


@app.route("/api/incidents")
def api_incidents():
    """Persistent incident list from Supabase, with live-memory fallback."""
    limit = min(int(request.args.get("limit", "100")), 500)
    rows = query_supabase_incidents(limit=limit)
    if rows is not None:
        return jsonify([supabase_row_to_api(row) for row in rows])
    with lock:
        return jsonify(list(incident_history)[:limit])


@app.route("/api/search")
def api_search():
    """Historical incident filtering for demos and dashboard controls."""
    limit = min(int(request.args.get("limit", "100")), 500)
    classification = request.args.get("classification")
    threat = request.args.get("threat")
    alert_level = request.args.get("alert_level", request.args.get("alertLevel"))
    hours = request.args.get("hours")

    alert_level = to_int(alert_level) if alert_level not in (None, "") else None
    hours = to_int(hours) if hours not in (None, "") else None

    rows = query_supabase_incidents(
        limit=limit,
        classification=classification,
        threat=threat,
        alert_level=alert_level,
        hours=hours,
    )
    if rows is not None:
        return jsonify([supabase_row_to_api(row) for row in rows])

    with lock:
        items = list(incident_history)
    if classification:
        items = [i for i in items if (i.get("classification") or "").upper() == classification.upper()]
    if threat:
        items = [i for i in items if (i.get("threat") or "").upper() == threat.upper()]
    if alert_level is not None:
        items = [i for i in items if (to_int(i.get("alertLevel")) or 0) >= alert_level]
    return jsonify(items[:limit])


@app.route("/api/stats")
def api_stats():
    """Dashboard KPI cards and charts."""
    limit = min(int(request.args.get("limit", "500")), 1000)
    rows = query_supabase_incidents(limit=limit)
    if rows is not None:
        return jsonify(summarize_incidents([supabase_row_to_api(row) for row in rows]))
    with lock:
        return jsonify(summarize_incidents(list(incident_history)))


@app.route("/api/export/incidents.csv")
def api_export_incidents_csv():
    """Download the local incident CSV dataset."""
    return send_file(CSV_FILE, as_attachment=True, download_name="sentinelmesh_incidents.csv")


if __name__ == "__main__":
    print(f"[SentinelMesh AI] Serving dashboard from: {FRONTEND_DIR}")
    print("[SentinelMesh AI] Dashboard -> http://localhost:5000")
    app.run(host="0.0.0.0", port=5000, debug=False)
