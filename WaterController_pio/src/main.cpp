/*
    WaterController
    See README.txt
*/

#if defined(ESP8266)
#include <coredecls.h>                  // settimeofday_cb()
#include <TZ.h>
#define MYTZ TZ_Asia_Jerusalem
#endif
#if defined(ESP32)
#include <lwip/apps/sntp.h>
#include "esp_sntp.h"
#define MYTZ "IST-2IDT,M3.4.4/26,M10.5.0" // Israel Standard/Daylight Time, matches ESP8266's TZ_Asia_Jerusalem
#endif

#include "Application.h"
#include <ArduinoJson.h>                // JSON parser
// #include "WebServer.h"
#include "lib/WiFiConnect.h"
#include "lib/MqttClient.h"
#include "lib/OTA.h"
#include "lib/ConnectionMonitor.h"
#include "lib/SerialDebug.h"
#include "lib/TimeUtils.h"
#include "lib/Log.h"
#include "lib/Profiler.h"

#include "main.h"

const char APP_NAME[] PROGMEM = "Water Controller";
const char APP_VER[]  PROGMEM = "1.00 build 20261031";

volatile time_t boottime = 0L;
volatile int timeset_cnt = 0;
uint32_t sketch_size = 0; // Set once in setup(): ESP.getSketchSize() hashes the whole image, about 160 ms

// WiFi
const char* SSID = _WIFI_SSID_ ;
const char* PWD  = _WIFI_PWD_;
const char* HOST  = _HOST_NAME_;

// OTA
const char* otaHostname = _HOST_NAME_;
const char* OTA_PASSWD  = _OTA_PASSWD_;

// MQTT
const char *announcementTopic = _AREA_ "/announcement/" _HOST_NAME_;
const char *lastWillTopic     = _AREA_ "/announcement/" _HOST_NAME_;

const char* mqtt_server = _MQTT_BROKER_;
const char* clientId = _HOST_NAME_;

#define TOPIC_COUNT 8
const char *topics[] = {
  TOPIC_ALIVE_GET,
  TOPIC_INFO_GET,
  TOPIC_VALVE_ENABLED_GET,
  TOPIC_VALVE_OPEN,
  TOPIC_VALVE_CLOSE,
  TOPIC_VALVE_STATE_GET,
  TOPIC_PRESSURE_GET,
  TOPIC_FLOW_RATE_GET
};

// JSON and temp buffers
DynamicJsonDocument doc(1024); // JSON
#define BUFFER_SIZE  (1024)
char BUF[BUFFER_SIZE];


static void update_boottime() {
    if (boottime == 0L) {
        time_t bt_new = time(nullptr) - millis()/1000;
        Serial.printf("\n--- settimeofday() called. Boot time initial: %u new: %u\n", (uint32_t)boottime, (uint32_t)bt_new);
        boottime = bt_new;
    }
    else {
        Serial.printf("\n--- settimeofday() called. Boot time: %u\n", (uint32_t)boottime);
    }
    timeset_cnt++;
}

#if defined(ESP8266)
void settime_cb() {
    update_boottime();
}
#endif
#if defined(ESP32)
void settime_cb(struct timeval *tv) {
    update_boottime();
}
#endif


// =======================================================
// SETUP
// =======================================================

void setup() {
  valve_init(); // First: take control of the valve outputs before anything else starts
  Serial.begin(115200);
  delay(2000);
  Serial.printf_P(PSTR("\n%s %s..\n"), FPSTR(APP_NAME), FPSTR(APP_VER));
  sketch_size = ESP.getSketchSize();

    // Time, SNTP
#if defined(ESP8266)
    settimeofday_cb(settime_cb);
    configTime(MYTZ, "pool.ntp.org");
#endif
#if defined(ESP32)
    sntp_set_time_sync_notification_cb(settime_cb);
    configTzTime(MYTZ, "pool.ntp.org");
#endif

    wifiConnect.setNonBlocking(true);
    wifiConnect.begin(SSID, PWD, HOST);

    static String hostname = wifiConnect.getHostname(); // static: must outlive setup(), as clientId/otaHostname point into its buffer
    clientId = hostname.c_str();    // Use the generated hostname as MQTT client ID
    otaHostname = hostname.c_str(); // Use the generated hostname for OTA as well

    otaClient.setStartCallback(valve_ota_start);
    otaClient.begin(otaHostname, OTA_PASSWD);

    mqttClient.begin(mqtt_server, clientId, true);
    mqttClient.setAnnouncementTopic(announcementTopic);
    mqttClient.setLastWillTopic(lastWillTopic);
    mqttClient.setTopics(topics, TOPIC_COUNT);
    mqttClient.setCallback(mqtt_callback);

    connectionMonitor.begin(TEST_WIFI_LED, test_change_event_callback, LED_PULLUP);
    connectionMonitor.setKeepAliveEventCallback(keepAlive_event_callback, KEEP_ALIVE_MESSAGE_INTERVAL);

    debug_init();

    ads_init();
    pressure_sensor_init(10000L); // Millis
    water_flow_sensor_init(60000L); // Millis

    profiler.begin(60000, 100, 1000, 10000, 300000);
}

void loop() {
    profiler.beginLoop();
    // webServer.handleClient();      // Listen for HTTP requests from clients
    mqttClient.handleClient();  // Listen for MQTT events from broker. Called every single cycle for maximum stability
    wifiConnect.handleClient(); // Required for WiFiConnect none-blocking version and reconnect functionality
    otaClient.handleClient();   // Required for OTA functionality
    connectionMonitor.handleClient(); // Monitor WiFi and MQTT connection
    serialDebug.handleClient(); // Serial debug command handler

    ads_handle();
    pressure_sensor_handle();
    water_flow_handle();
    valve_handle();

    serialDebug.handleClient();
    profiler.endLoop();
}


// Adds app/version/host/time/network identity fields to the JSON document.
static void add_identity_info(DynamicJsonDocument &doc, time_t now) {
    doc["app"] = FPSTR(APP_NAME);
    doc["ver"] = FPSTR(APP_VER);
    doc["host"] = HOST;
    doc["ts"] = (uint32_t)now;
    doc["now"] = formatTime(now);

    time_t boottime_local = boottime;
    if (boottime == 0 && now > MIN_TIMESTAMP) {
        boottime_local = now - (millis() / 1000);
        doc["boottime_correction"] = true;
    }

    doc["boottime"] = (uint32_t)boottime_local;
    doc["boot"] = formatTime(boottime_local);

    doc["MAC"] = wifiConnect.getMAC();
    doc["IP"] = wifiConnect.getIP();
    doc["gateway"] = wifiConnect.getGatewayIPAddress();
    doc["hostname"] = wifiConnect.getHostname();
    doc["mdns"] = otaClient.getHostname();
    doc["RSSI"] = wifiConnect.getRSSI();
    doc["phymode"] = wifiConnect.getPhyModeString();
    doc["chan"] = wifiConnect.getChannel();
    doc["BSSID"] = wifiConnect.getBSSID();
    doc["timeset"] = timeset_cnt;
}

// Adds MCU/runtime diagnostics (heap, CPU, internal temperature) to the JSON document.
static void add_runtime_info(DynamicJsonDocument &doc) {
#if defined(ESP8266)
    doc["mc"] = F("ESP8266");
#endif
#if defined(ESP32)
    doc["mc"] = F("ESP32");
#endif
    doc["freeHeap"] = ESP.getFreeHeap();
#if defined(ESP8266)
    doc["heapFragmentation"] = ESP.getHeapFragmentation();
    doc["maxFreeBlockSize"] = ESP.getMaxFreeBlockSize();
#endif
    doc["cpuFreqMHz"] = ESP.getCpuFreqMHz();
#if defined(ESP8266)
    doc["coreVer"] = ESP.getCoreVersion();
#endif
    doc["sdkVer"] = ESP.getSdkVersion();
    doc["sketchSize"] = sketch_size;
    doc["freeSketchSpace"] = ESP.getFreeSketchSpace();
    doc["flashChipSize"] = ESP.getFlashChipSize();
}


// Adds WiFi/MQTT connection-loss history to the JSON document.
static void add_connection_info(DynamicJsonDocument &doc) 
{
    doc["wifi_lost"] = (uint32_t)connectionMonitor.wifi_lost_timestamp;
    doc["mqtt_lost"] = (uint32_t)connectionMonitor.mqtt_lost_timestamp;
    doc["wifiLostCnt"] = connectionMonitor.wifi_lost_cnt;
    doc["mqttLostCnt"] = connectionMonitor.mqtt_lost_cnt;
    doc["wifiLostStatus"] = connectionMonitor.wifi_lost_status;
    doc["mqttLostState"] = connectionMonitor.mqtt_lost_state;
    doc["wifi_conn"] = (uint32_t)connectionMonitor.wifi_conn_timestamp;
    doc["mqtt_conn"] = (uint32_t)connectionMonitor.mqtt_conn_timestamp;
    if (connectionMonitor.wifi_lost_timestamp > 0L) {
        doc["wifiLost"] = formatTime(connectionMonitor.wifi_lost_timestamp);
    }
    if (connectionMonitor.mqtt_lost_timestamp > 0L) {
        doc["mqttLost"] = formatTime(connectionMonitor.mqtt_lost_timestamp);
    }
    if (connectionMonitor.wifi_conn_timestamp > 0L) {
        doc["wifiConn"] = formatTime(connectionMonitor.wifi_conn_timestamp);
    }
    if (connectionMonitor.mqtt_conn_timestamp > 0L) {
        doc["mqttConn"] = formatTime(connectionMonitor.mqtt_conn_timestamp);
    }
    doc["badIp"] = connectionMonitor.bad_dhcp_ip_assigned;
}


enum InfoLevel {
    INFO_BASIC,
    INFO_WITH_CONNECTION
};


void send_info(const char* topic, InfoLevel level)
{
    time_t now = time(nullptr);

    doc.clear();
    add_identity_info(doc, now);
    add_runtime_info(doc);
    if (level == INFO_WITH_CONNECTION) {
        add_connection_info(doc);
    }

    if (doc.overflowed()) {
        Serial.printf("WARN: send_info: JSON document exceeded its capacity (%u bytes); fields were dropped\n", (unsigned)doc.capacity());
    }

    size_t len = serializeJson(doc, BUF, BUFFER_SIZE);
    if (len >= BUFFER_SIZE - 1) {
        Serial.printf("WARN: send_info: JSON payload may be truncated (%u bytes, buffer %u)\n", (unsigned)len, (unsigned)BUFFER_SIZE);
    }
    mqttClient.publish(topic, BUF);
}




void mqtt_callback(char* topic, byte* payload, unsigned int length) {
  LOG("mqtt_callback called");
  if(strcmp(topic, TOPIC_VALVE_OPEN) == 0) {
    // if (payload[0] == '1') {
    valve_open();
  } else if(strcmp(topic, TOPIC_VALVE_CLOSE) == 0) {
    valve_close();
  } else if(strcmp(topic, TOPIC_VALVE_STATE_GET) == 0) {
    valve_state_publish();
  } else if(strcmp(topic, TOPIC_PRESSURE_GET) == 0) {
    pressure_get();
  } else if(strcmp(topic, TOPIC_FLOW_RATE_GET) == 0) {
    water_flow_get();
  } else if(strcmp(topic, TOPIC_ALIVE_GET) == 0) {
    send_info(TOPIC_ALIVE, INFO_BASIC);
  } else if(strcmp(topic, TOPIC_INFO_GET) == 0) {
    send_info(TOPIC_INFO, INFO_WITH_CONNECTION);
  }
}

void test_change_event_callback(uint8_t event) {
  if (event == EVENT_MQTT_RESTORED) {
    send_info(TOPIC_CONNECTION_INFO, INFO_WITH_CONNECTION);
    keepAlive_event_callback();
    valve_state_publish(); // Changes made while MQTT was down were not delivered
    pressure_status_publish();
  }
}


void keepAlive_event_callback() 
{
    if (connectionMonitor.mqttConnected()) {
#if defined(_SN_DEBUG_)
        snprintf (BUF, BUFFER_SIZE, "connected %d %s %s", wifiConnect.getRSSI(), wifiConnect.getBSSID().c_str(), wifiConnect.getPhyModeString());
        mqttClient.publish(TOPIC_KEEPALIVE, BUF);
#else
        mqttClient.publish(TOPIC_KEEPALIVE, "connected");
#endif
    }
}
