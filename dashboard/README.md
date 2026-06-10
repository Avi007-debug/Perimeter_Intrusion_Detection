# Perimeter Dashboard

Static offline dashboard for the AI Perimeter Intrusion Detection Network.

## Run locally

```powershell
cd C:\Coding\IoT_Project\dashboard
python -m http.server 8765 --bind 127.0.0.1
```

Open:

```text
http://127.0.0.1:8765/
```

## Add gateway incidents

After an incident is saved, the ESP32 gateway prints a line like:

```text
JSON        : {"time":"00:12:44","direction":"EAST -> WEST",...}
```

Copy only the JSON object after `JSON        :`, paste it into the dashboard text area, and click `Add incident`.

The same JSON shape can later be sent automatically from a Flask serial bridge.
