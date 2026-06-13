# ESP-NOW Troubleshooting Guide

This document captures critical issues encountered and resolved when configuring ESP-NOW between an ESP32 Gateway and ESP8266 Sensor Nodes, specifically when the Gateway is connected to an external Wi-Fi network (like a mobile hotspot).

## 1. The Wi-Fi Channel Mismatch Bug
**Symptom:** Nodes connect briefly at boot, but drop offline (`ONLINE: 0`) moments later. No data is received by the Gateway.
**Root Cause:**
- ESP-NOW requires all devices to communicate on the exact same Wi-Fi channel.
- The Gateway (ESP32) initializes ESP-NOW on the default channel (Channel 1).
- When the Gateway connects to an external Wi-Fi network (e.g., a mobile hotspot) via `WiFi.begin()`, its physical radio automatically changes channels to match the router (e.g., Channel 6 or 11).
- The sensor nodes (ESP8266) remain hardcoded to Channel 1, causing a total loss of communication.
**The Fix:**
Sensor nodes must scan for the target Wi-Fi network on boot, extract its channel, and physically lock their radio to that channel before initializing ESP-NOW.
*For ESP8266, this requires `user_interface.h` to access `wifi_set_channel()`:*
```cpp
int channel = 1;
int n = WiFi.scanNetworks();
for (int i = 0; i < n; i++) {
  if (WiFi.SSID(i) == "Your_Hotspot_Name") {
    channel = WiFi.channel(i);
    break;
  }
}
wifi_promiscuous_enable(1);
wifi_set_channel(channel);
wifi_promiscuous_enable(0);
```

## 2. The WIFI_AP_STA MAC Address Bug
**Symptom:** The channels match perfectly, the ESP8266 successfully adds the peer, but `esp_now_send` constantly returns `FAILED` (No ACK received).
**Root Cause:**
- When an ESP32 is set to `WIFI_AP_STA` mode, it simultaneously creates a Station interface (STA) and an Access Point interface (AP).
- Even if the ESP32 is acting as a Station to connect to a router, the internal ESP-NOW library often permanently binds its listening socket to the **AP MAC address** rather than the STA MAC address.
- Sending ESP-NOW packets to the STA MAC address results in the packets being received, but the low-level WiFi driver throws them away and refuses to send an ACK back, causing the sender to report a failure.
**The Fix:**
Print both MAC addresses from the Gateway:
```cpp
Serial.println(WiFi.macAddress());       // STA MAC
Serial.println(WiFi.softAPmacAddress()); // AP MAC
```
Configure the sensor nodes' `receiverMAC` array to use the **AP MAC address** (which usually ends in an incremented hex value, e.g., `BD` instead of `BC`).
