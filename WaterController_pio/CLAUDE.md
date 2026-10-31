# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

ESP32 (Wemos D1 mini32, Arduino framework) firmware for a smart-home water controller: drives a motorized ball valve via an H-bridge (2 relays / DRV8871), reads water pressure through an ADS1115 ADC over I2C, counts flow-meter pulses (YF-B10, 476 pulses/L) on an interrupt, and talks to the home automation system over MQTT. Built with PlatformIO.

## Build / upload / monitor

```sh
pio run -e RELEASE_OTA_esp32dv                 # build
pio run -e RELEASE_OTA_esp32dv -t upload       # flash over the air (espota) to host "watercontroller1"
pio run -e RELEASE_COM_esp32dv -t upload       # flash over serial (COM7)
pio device monitor -e RELEASE_COM_esp32dv      # serial monitor, 115200 baud
```

The build requires these environment variables, injected as string macros via `platformio.ini`:
`SMARTHOME_WIFI_SSID`, `SMARTHOME_WIFI_PWD`, `SMARTHOME_MQTT_BROKER`, `SMARTHOME_OTA_PASSWD` (and the `_OFFICE` variants when `[common] build_flags` is switched to `build_flags_office`). They become `_WIFI_SSID_`, `_WIFI_PWD_`, `_MQTT_BROKER_`, `_OTA_PASSWD_`. Per-env `hostname`/`area` become `_HOST_NAME_` / `_AREA_`.

There are no unit tests (`test/` is the empty PlatformIO placeholder). Verification is done on hardware via the serial monitor and MQTT.

## Architecture

- **Cooperative, non-blocking super-loop.** `setup()` in `src/main.cpp` initializes everything; `loop()` calls each module's `handleClient()` / `*_handle()` every iteration. Nothing may block (`delay()`, waiting loops): timing is done with `millis()` deltas inside the handle functions (e.g. valve auto-stop after `valve_move_time_mills`, periodic pressure/flow publishing). `profiler` measures loop latency.
- **`src/lib/`** — reusable infrastructure singletons shared across the author's SmartHome projects (each file carries its own version history in header comments): `wifiConnect` (non-blocking connect/reconnect, PHY mode fallback, hostname handling), `mqttClient` (PubSubClient wrapper: reconnect, announcement + last-will topic, resubscribes the `topics[]` list on reconnect), `otaClient` (ArduinoOTA), `connectionMonitor` (WiFi/MQTT health, status LED, lost/restored events, keep-alive callback), `serialDebug` (serial command console), `profiler`, `Log` (`LOG`/`LOG_ERROR`), `TimeUtils`. Keep changes there generic and code still `#if defined(ESP8266)` / `ESP32` compatible. `*.prev` files are old backups, not compiled.
- **ArduinoOTA owns mDNS.** Don't call `MDNS.begin()` again elsewhere (see the comment in `src/lib/WiFiConnect.h`); it breaks OTA discovery.
- **Application modules** in `src/` are plain C-style functions declared in `src/main.h`: `valve.cpp`, `pressure_sensor.cpp` (uses `ads.cpp`), `water_flow.cpp`, `debug.cpp` (registers serial-console commands with `serialDebug`). GPIO assignments live in `src/Application.h`.
- **MQTT topics** are compile-time string macros in `src/main.h`, all of the form `<_AREA_>/<_HOST_NAME_>/...` (e.g. `home/watercontroller1/valve/open`). To add an inbound command: define the topic in `main.h`, add it to `topics[]` and bump `TOPIC_COUNT` in `main.cpp`, then dispatch it in `mqtt_callback()`. `*/get` topics trigger a reply on the corresponding non-`get` topic.
- **Hostname flow:** after `wifiConnect.begin()`, the resolved hostname is reused as MQTT client ID and OTA hostname (held in a `static String` in `setup()` because the clients keep the raw pointer).
- Version string `APP_VER` in `main.cpp` is bumped manually (`"x.yy build YYYYMMDD"`).
- Some comments are in Russian.
