// =======================================================
// DEBUG 
// =======================================================
#include "Application.h"
#include "lib/SerialDebug.h"
#include "lib/WiFiConnect.h"
#include "lib/MqttClient.h"
#include "lib/OTA.h"
#include "lib/ConnectionMonitor.h"
#include "lib/TimeUtils.h"
#include "main.h"

void main_debug_info();


void wifiConnect_debug_info() {
  wifiConnect.debug_info();
}


void connection_monitor_debug_info() {
    connectionMonitor.debug_info();
}


void wifi_reconnect() {
  wifiConnect.try_reconnect();
}


void wifi_set_11b() {
  wifiConnect.set_phy_11b();
  wifiConnect.try_reconnect();
}


void wifi_set_11g() {
  wifiConnect.set_phy_11g();
  wifiConnect.try_reconnect();
}


void wifi_set_11n() {
  wifiConnect.set_phy_11n();
  wifiConnect.try_reconnect();
}


void otaClient_debug_info() {
  otaClient.debug_info();
}


void mqttClient_debug_info() {
  mqttClient.debug_info();
}


void restart() {
  valve_restart_prepare();
  ESP.restart();
}

void main_debug_info() {

    time_t now = time(nullptr);
    Serial.println("\nmain.cpp:\n=========");
    Serial.printf("AppName: %s\n", FPSTR(APP_NAME));
    Serial.printf("Version: %s\n", FPSTR(APP_VER));
    Serial.printf("Now timestamp: %u\n", (uint32_t)now);
    Serial.printf("Now: %s\n", formatTime(now));
    Serial.printf("Boot timestamp: %u\n", (uint32_t)boottime);
    Serial.printf("Boot time: %s\n", formatTime(boottime));
    Serial.printf("timeset_cnt: %d\n", timeset_cnt);

    // debug:
#if defined(ESP8266)
    Serial.printf("MC: ESP8266\n");
#endif
#if defined(ESP32)
    Serial.printf("MC: ESP32\n");
#endif
    Serial.printf("FreeHeap: %u\n", ESP.getFreeHeap());
#if defined(ESP8266)
    Serial.printf("HeapFragmentation: %u\n", ESP.getHeapFragmentation());
    Serial.printf("MaxFreeBlockSize: %u\n", ESP.getMaxFreeBlockSize());
#endif
    Serial.printf("CpuFreqMHz: %u\n", ESP.getCpuFreqMHz());
#if defined(ESP8266)
    Serial.printf("CoreVersion: %s\n", ESP.getCoreVersion().c_str());
#endif
    Serial.printf("SdkVersion: %s\n", ESP.getSdkVersion());
    Serial.printf("SketchSize: %u\n", sketch_size);
    Serial.printf("FreeSketchSpace: %u\n", ESP.getFreeSketchSpace());
    Serial.printf("FlashChipSize: %u\n", ESP.getFlashChipSize());
#if defined(ESP8266)
    Serial.printf("FlashChipRealSize: %u\n", ESP.getFlashChipRealSize());
#endif
}


void debug_init() {
  serialDebug.addInfoCommandHandler(main_debug_info);
  serialDebug.addInfoCommandHandler(wifiConnect_debug_info);
  serialDebug.addInfoCommandHandler(otaClient_debug_info);
  serialDebug.addInfoCommandHandler(mqttClient_debug_info);
    serialDebug.addInfoCommandHandler(connection_monitor_debug_info);
  serialDebug.addCommandHandler("wifi_reconnect", wifi_reconnect);
  serialDebug.addCommandHandler("wifi_set_11b", wifi_set_11b);
  serialDebug.addCommandHandler("wifi_set_11g", wifi_set_11g);
  serialDebug.addCommandHandler("wifi_set_11n", wifi_set_11n);
  serialDebug.addCommandHandler("wifi_info", wifiConnect_debug_info);
  serialDebug.addCommandHandler("mqtt_info", mqttClient_debug_info);
  serialDebug.addCommandHandler("restart", restart);
}