# Smart Agricultural Disease Monitor (ESP32-CAM)

This repository contains firmware and a web UI for an ESP32-CAM based smart agricultural disease monitoring system. The ESP32 streams camera frames, exposes capture endpoints, and forwards sensor data via MQTT to a web client which performs AI-based disease analysis.

## Contents
- ESP32 firmware: `ESP32_CAM_MASTER/ESP32_CAM_MASTER.ino`
- Arduino sensor slave: `arduino_slave/arduino_slave.ino`
- Web UI: `index.html`

## Overview
The ESP32-CAM firmware initializes the camera (SVGA by default when PSRAM is available), starts a small HTTP server and connects to an MQTT broker. The web UI connects to the broker over WebSockets to receive sensor updates and to control the camera/pump. The Arduino slave produces sensor data over Serial which the ESP forwards to MQTT.

Key design goals:
- Reliable MJPEG streaming to web browsers
- Low-latency sensor forwarding via MQTT
- Robust Wi‑Fi with AP fallback for configuration
- Memory-conscious camera settings (PSRAM-aware)

## Firmware Highlights
- Camera initialization: requests `FRAMESIZE_SVGA` and uses PSRAM when found. Tuned `jpeg_quality` and `fb_count` for best FPS vs quality.
- HTTP endpoints:
  - `/stream` — multipart MJPEG stream (multipart/x-mixed-replace)
  - `/capture` — trigger capture and hold latest frame
  - `/capture.jpg` — returns a JPEG capture (sent in 1KB chunks)
  - `/config` — simple web form to set Wi‑Fi credentials
  - `/saveconfig` — saves credentials to Preferences and reboots
  - `/reboot` — soft reboot
- Streaming fixes: image data is written in small chunks to avoid large blocking socket writes (prevents white frames/timeouts). `yield()` is used in the loop to allow background work.
- CORS: single `Access-Control-Allow-Origin: *` header used to avoid duplicate value errors in browsers.

## MQTT
- Broker used (example): `test.mosquitto.org`
- ESP MQTT topics:
  - Publish sensor JSON: `esp32/cam/sensors`
  - Publish status: `esp32/cam/status`
  - Subscribe for commands: `esp32/cam/command` (e.g., `PUMP_ON`, `PUMP_OFF`)
- Web UI uses Paho MQTT over WebSockets (port 8080 on public brokers) and subscribes/publishes to the same topics for real-time integration.

## Serial / Arduino Integration
- The Arduino slave sends sensor data over Serial in the format: `temp:25.00,humidity:60.00,soil:500`.
- ESP reads this line, converts to JSON like `{ "temperature":25.00, "humidity":60.00, "soilMoisture":500, "arduinoConnected":true }`, and publishes to `esp32/cam/sensors`.
- ESP serial is configured to `115200` (ensure Arduino side matches or use a separate Serial port for device comms).

## Build & Upload
1. Install Arduino core for ESP32 and select the correct board (AI Thinker ESP32-CAM or equivalent).
2. In Tools: enable `PSRAM` if present, select `Upload Speed` (e.g., 115200 or 921600), and set the correct `Flash Size` per your board.
3. Open `ESP32_CAM_MASTER/ESP32_CAM_MASTER.ino`, configure Wi‑Fi credentials (or use the `/config` page to save them), then upload.

## Usage
- On boot the ESP will attempt STA connect; if it fails it starts an AP `ESP32-CAM-Setup` for local configuration.
- When STA connected, the firmware prints `WiFi connected. IP: <ip>` on Serial — open that IP in a browser and the `/stream` endpoint will show the live camera.
- Web UI (index.html) connects to the MQTT broker via WebSockets and receives sensor data and AI analysis results.

## Troubleshooting
- White/blank stream: older code attempted large single socket writes which can block the client. The firmware now writes image data in 1KB chunks to avoid blocking.
- Browser CORS error about duplicate `Access-Control-Allow-Origin`: ensure the firmware sends the header only once (fixed in current firmware).
- Missing JS resource (e.g., `language.js`) will show 404s in browser console but won't affect core streaming — ensure web UI files are served by your dev server.
- Serial bootloader gibberish: bootloader prints appear if Serial baud doesn't match; set Serial to `115200` in Serial Monitor to match firmware.
- MQTT connectivity: public brokers may block WebSocket on some ports; verify broker WebSocket port (8080) and use a local broker for testing if needed.

## Known Limitations & Next Steps
- No runtime UI to change camera `jpeg_quality`/`framesize` — adding a `/setconfig` endpoint and UI controls would allow live tuning.
- Arduino↔ESP serial speed mismatch: either change Arduino to `115200` or use a dedicated UART (Serial1) for Arduino comms to preserve Serial debug logs.
- Security: MQTT is currently plaintext; consider using an authenticated and TLS-enabled broker for production.

## Files of Interest
- `ESP32_CAM_MASTER/ESP32_CAM_MASTER.ino` — main firmware (camera, HTTP server, MQTT, Serial handling)
- `arduino_slave/arduino_slave.ino` — sensor reading and pump control (Serial output format described above)
- `index.html` — web UI that connects to MQTT over WebSockets and performs AI analysis

## License
MIT — adapt as required for your project.

---
If you want, I can: add a runtime camera quality toggle endpoint and UI, update the Arduino serial handling to use Serial1, or produce a short CONTRIBUTING.md and example MQTT broker configuration. Tell me which you'd like next.
