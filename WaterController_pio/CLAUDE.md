# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

ESP32 (ESP32-DevKitC-32U, PlatformIO `board = esp32dev`, Arduino core 2.0.18 from the Tasmota platform build) firmware for a smart-home water controller: drives a motorized ball valve via an H-bridge (DRV8871 on the current schematic), reads water pressure (SWP-300, 0-10 bar, 0-5 V) through an ADS1115 ADC over I2C, counts flow-meter pulses (YF-B10, 476 pulses/L) on an interrupt, and talks to the home automation system over MQTT. The valve currently moves only on MQTT commands: the firmware has no leak-detection or auto-shutoff logic yet.

The git root is the parent directory. It also holds `doc/` (`water-connection-project-summary.md` is the hardware overview, in Russian) and `schematics/` (KiCad; the current design is `schematics/WaterController/`).

## Build / upload / monitor

```sh
pio run -e RELEASE_OTA_esp32dv                 # build
pio run -e RELEASE_OTA_esp32dv -t upload       # flash over the air (espota) to host "watercontroller1"
pio run -e RELEASE_COM_esp32dv -t upload       # flash over serial (COM7)
pio device monitor -e RELEASE_COM_esp32dv      # serial monitor, 115200 baud
```

There is no `default_envs`, so a bare `pio run` builds both environments. Includes such as `"lib\Log.h"` use backslashes, so the sources build on Windows only.

The build requires these environment variables, injected as string macros via `platformio.ini`:
`SMARTHOME_WIFI_SSID`, `SMARTHOME_WIFI_PWD`, `SMARTHOME_MQTT_BROKER`, `SMARTHOME_OTA_PASSWD` (and the `_OFFICE` variants when `[common] build_flags` is switched to `build_flags_office`). They become `_WIFI_SSID_`, `_WIFI_PWD_`, `_MQTT_BROKER_`, `_OTA_PASSWD_`. Per-env `hostname`/`area` become `_HOST_NAME_` / `_AREA_`.

There are no unit tests (`test/` is the empty PlatformIO placeholder). Verification is done on hardware via the serial monitor and MQTT. The serial console accepts `help`, `info`, and the commands registered in `src/debug.cpp`.

## Architecture

- **Cooperative, non-blocking super-loop.** `setup()` in `src/main.cpp` initializes everything; `loop()` calls each module's `handleClient()` / `*_handle()` every iteration. Nothing may block (`delay()`, waiting loops): timing is done with `millis()` deltas inside the handle functions (e.g. valve auto-stop after `valve_move_time_mills`, periodic pressure/flow publishing). `profiler` measures loop latency. The one exception is `ads_voltage_chnnel_get()` in `ads.cpp`, which polls for conversion with a timeout of about 50 ms. It replaces `ads.readADC_SingleEnded()`, which hangs forever if the ADS1115 disappears, so don't switch back.
- **`src/lib/`** — reusable infrastructure singletons shared across the author's SmartHome projects (each file carries its own version history in header comments): `wifiConnect` (non-blocking connect/reconnect, PHY mode fallback, hostname handling), `mqttClient` (PubSubClient wrapper: reconnect, announcement + last-will topic, resubscribes the `topics[]` list on reconnect), `otaClient` (ArduinoOTA), `connectionMonitor` (WiFi/MQTT health, status LED, lost/restored events, keep-alive callback), `serialDebug` (serial command console), `profiler`, `Log` (`LOG`/`LOG_ERROR`), `TimeUtils`. Keep changes there generic and code still `#if defined(ESP8266)` / `ESP32` compatible, even though this application is ESP32-only (`Application.h` raises `#error` on ESP8266). `*.prev` files are old backups, not compiled.
- **ArduinoOTA owns mDNS.** Don't call `MDNS.begin()` again elsewhere (see the comment in `src/lib/WiFiConnect.h`); it breaks OTA discovery.
- **Application modules** in `src/` are plain C-style functions declared in `src/main.h`: `valve.cpp`, `pressure_sensor.cpp` (uses `ads.cpp`), `water_flow.cpp`, `temperature.cpp` (DS18B20 probes, one per GPIO), `box_sensor.cpp` (SHT41 or SHTC3 on the ADS1115 I2C bus, raw `Wire` commands to stay non-blocking), `alarms.cpp` (freeze/overheat/condensation flags from the cached readings, LED4), `debug.cpp` (registers serial-console commands with `serialDebug`). GPIO assignments live in `src/Application.h`.
- **The valve has no position feedback.** `VALVE_OPEN_STATUS` / `VALVE_CLOSE_STATUS` read the IN1/IN2 levels (driven by the firmware and the physical buttons, which bypass the ESP32), not the valve position. `valve/state` is inferred: firmware times the 7 s travel, and a button hold counts as full travel only if it lasted that long. A pressed button overrides firmware moves and blocks commands.
- **MQTT topics** are compile-time string macros in `src/main.h`, mostly of the form `<_AREA_>/<_HOST_NAME_>/...` (e.g. `home/watercontroller1/valve/open`). Exceptions: `<_AREA_>/alive[/get]` is shared by all devices in the area, and keep-alive and announcement use `<_AREA_>/keepalive/<_HOST_NAME_>` and `<_AREA_>/announcement/<_HOST_NAME_>`. To add an inbound command: define the topic in `main.h`, add it to `topics[]` and bump `TOPIC_COUNT` in `main.cpp`, then dispatch it in `mqtt_callback()`. `*/get` topics trigger a reply on the corresponding non-`get` topic.
- **Opening bans.** Opening by firmware needs Lock Switch J7 off (`VALVE_LOCK_GPIO` 13 = 0, it cuts IN1 in hardware and also blocks SW1) and `valve_enable` = 1 (NVS, MQTT `valve_enable/set`; does not block SW1). Otherwise `valve/rejected`. Closing must always work.
- **Hostname flow:** after `wifiConnect.begin()`, the resolved hostname is reused as MQTT client ID and OTA hostname (held in a `static String` in `setup()` because the clients keep the raw pointer).
- Version string `APP_VER` in `main.cpp` is bumped manually (`"x.yy build YYYYMMDD"`), with a matching entry in the `README.txt` version list.
- Some comments are in Russian.
