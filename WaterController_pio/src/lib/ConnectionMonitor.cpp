// ConnectionMonitor
//
// WiFi & MQTT connection monitoring, status indication LED, and recovery management:
//
//  * LED OFF      - WiFi not connected
//  * LED ON       - WiFi and MQTT connected
//  * LED blinking - WiFi connected, MQTT not connected
//
// LED connection: pull-up (default)
//                                    --- VCC
//             +-----------+    LED    |
//     GPIO ---| 220R-510R |----|<|----+
//             +-----------+
//
// LED connection: pull-down
//             +-----------+    LED
//     GPIO ---| 220R-510R |----|>|----+
//             +-----------+           |
//                                    --- GND
//
// Suggested LED connection method:
//    ESP32: pull-up
//    ESP8266: pull-up (D3, D4, D9, D10), pull-down (D0)
//
// Version history:
// v0.2 20220129
// v0.3 20220614 - Fix not updated wifi_conn_timestamp value
// v0.4 20220616 - Test and KeepAlive event
// v0.5 20221014 - Fire WiFi and MQTT connect and lost events
// v0.6 20221219 - Fix mqtt connection time when NTP received firs time
// v0.7 20230304 - If wifi connected, but mqtt is not connected and network id is 192.168.66.0, try reconnect
// v0.8 20230519 - If wifi connected, but mqtt is not connected and GetwayIP is not set, try reconnect
// v0.9 20231009 - Added default output LED level to begin().
// v0.10 20231206 - Initialization fix
// v0.11 20251030 - DEBUG_BAD_IP (to enable debug bad IP, add it to platformio.ini as -DDEBUG_BAD_IP)
// v0.12 20260327 - Get MQTT state when MQTT connection is lost
// v0.13 20260401 - Set LED_PULLUP as default
// v0.14 20260606 - Refactoring, AI suggested improvements, etc...
// v0.15 20260607 - Added MQTT lost event when WiFi is lost and MQTT was connected
// v0.16 20260608 - Minor fixes
// v0.17 20260611 - Fix late-SNTP connection timestamp correction (anchor to uptime). LED pin as a parameter
// v0.18 20260613 - Rename from Test to ConnectionMonitor; clarify LED connection terminology
// v0.19 20260615 - Minor fix for mqtt_lost_state when WiFi lost 

#include <time.h>                       // time() ctime()
#include "Application.h"
#include "ConnectionMonitor.h"
#include "WiFiConnect.h"
#include "MqttClient.h"
#include "Log.h"
#include "TimeUtils.h"

ConnectionMonitor connectionMonitor;

ConnectionMonitor::ConnectionMonitor() {
    m_ledConnection = LED_PULLUP;  // Default pull-up connection for better compatibility with ESP8266 and ESP32.
}


void ConnectionMonitor::begin(uint8_t ledPin) {
    m_ledPin = ledPin;
    pinMode(m_ledPin, OUTPUT);
    if (m_ledConnection == LED_PULLDOWN) {
        m_ledOn = HIGH;
        LOG("ConnectionMonitor: LED_PULLDOWN GPIO: ", m_ledPin);
    }
    else {
        m_ledOn = LOW;
        LOG("ConnectionMonitor: LED_PULLUP GPIO: ", m_ledPin);
    }
    m_ledOff = !m_ledOn;
    digitalWrite(m_ledPin, m_ledOff);
}


void ConnectionMonitor::begin(uint8_t ledPin, void (*change_event_callback_ptr)(uint8_t event)) {
    setChangeEventCallback(change_event_callback_ptr);
    begin(ledPin);
}


void ConnectionMonitor::begin(uint8_t ledPin, bool ledConnection) {
    this->m_ledConnection = ledConnection;
    begin(ledPin);
}


void ConnectionMonitor::begin(uint8_t ledPin, void (*change_event_callback_ptr)(uint8_t event), bool ledConnection) {
    this->m_ledConnection = ledConnection;
    begin(ledPin, change_event_callback_ptr);
}


void ConnectionMonitor::setChangeEventCallback(void (*change_event_callback_ptr)(uint8_t event)) {
    this->m_changeEventCallback = change_event_callback_ptr;
}


void ConnectionMonitor::setTestEventCallback(void (*test_event_callback_ptr)()) {
    this->m_testEventCallback = test_event_callback_ptr;
}


void ConnectionMonitor::setKeepAliveEventCallback(void (*keepalive_event_callback_ptr)(), unsigned long interval) {
    this->m_keepaliveCallback = keepalive_event_callback_ptr;
    this->m_keepaliveInterval = interval;
    this->m_keepaliveCheckTime = millis();
}


void ConnectionMonitor::handleClient() {
    check_connection();
}


bool ConnectionMonitor::wifiConnected() {
    return wifiConnect.connected();
}


bool ConnectionMonitor::mqttConnected() {
    return wifiConnect.connected() && mqttClient.connected();
}


void ConnectionMonitor::fire_change_event(uint8_t event) {
    if (m_changeEventCallback != nullptr) {
        m_changeEventCallback(event);
    }
}


void ConnectionMonitor::fire_test_event() {
    if (m_testEventCallback != nullptr) {
        m_testEventCallback();
    }
}


void ConnectionMonitor::fire_keepalive_event() {
    if (m_keepaliveCallback != nullptr) {
        m_keepaliveCallback();
    }
}


/***
 * LED OFF      - WiFi not connected
 * LED ON       - WiFi and MQTT connected
 * LED blinking - WiFi connected, MQTT not connected
 */
void ConnectionMonitor::check_connection() {

    unsigned long t = millis();
    if (t - m_connectionCheckTime >= CONNECTION_MONITOR_INTERVAL) {
        m_connectionCheckTime = t;
        if (wifiConnect.connected()) {
            if (wifi_conn_timestamp == 0) {
                wifi_conn_timestamp = time(nullptr);
                fire_change_event(EVENT_WIFI_RESTORED);
                LOG("EVENT_WIFI_RESTORED: ", (ulong)wifi_conn_timestamp);
            }
            else {
                if (wifi_conn_timestamp <= MIN_TIMESTAMP) {
                    time_t tm = time(nullptr);
                    if (tm > MIN_TIMESTAMP) {
                        LOG("wifi_conn_timestamp changed from: ", (ulong)wifi_conn_timestamp);
                        // Pre-sync time() counts from ~0 at boot, so the stored value is the
                        // uptime (sec) at connect. Anchor it to real time via current uptime:
                        wifi_conn_timestamp = tm - (millis() / 1000 - wifi_conn_timestamp);
                        LOG("wifi_conn_timestamp changed to: ", (ulong)wifi_conn_timestamp);
                    }
                }
            }
            if (mqttClient.connected()) {
                digitalWrite(m_ledPin, m_ledOn);

                if (mqtt_conn_timestamp == 0L) {
                    mqtt_conn_timestamp = time(nullptr);
                    // MQTT Restored. Notify it
                    fire_change_event(EVENT_MQTT_RESTORED);
                    LOG("EVENT_MQTT_RESTORED: ", (ulong)mqtt_conn_timestamp);
                }
                else {
                    if (mqtt_conn_timestamp <= MIN_TIMESTAMP) {
                        time_t tm = time(nullptr);
                        if (tm > MIN_TIMESTAMP) {
                            LOG("mqtt_conn_timestamp changed from: ", (ulong)mqtt_conn_timestamp);
                            // Pre-sync time() counts from ~0 at boot, so the stored value is the
                            // uptime (sec) at connect. Anchor it to real time via current uptime:
                            mqtt_conn_timestamp = tm - (millis() / 1000 - mqtt_conn_timestamp);
                            LOG("mqtt_conn_timestamp changed to: ", (ulong)mqtt_conn_timestamp);
                        }
                    }
                }
            }
            else {
                digitalWrite(m_ledPin, !digitalRead(m_ledPin));
                if (mqtt_conn_timestamp > 0L) {
                    mqtt_lost_cnt++;
                    mqtt_lost_state = mqttClient.getState();
                    mqtt_conn_timestamp = 0L;
                    mqtt_lost_timestamp = time(nullptr);
                    fire_change_event(EVENT_MQTT_LOST);
                    LOG("EVENT_MQTT_LOST: ", (ulong)mqtt_lost_timestamp);
                }
                if (t - m_ipAssignedCheckTime >= CONNECTION_MONITOR_IP_ASSIGNED_INTERVAL) {
                    m_ipAssignedCheckTime = t;
                    if (!wifiConnect.isIPAddressSet(wifiConnect.getGatewayIPAddress())) {
                        bad_dhcp_ip_assigned++;
                        // To debug bad IP, set DEBUG_BAD_IP in the platformio.ini
                        #ifdef DEBUG_BAD_IP
                        if (!wifiConnect.isIPAddressSet(wifiConnect.getGatewayIPAddress())) {
                            Serial.println("\nWARNING: ConnectionMonitor identified BAD Gateway IP Address\n");
                        }
                        wifiConnect.debug_info();
                        #endif
                        wifiConnect.try_reconnect();
                    }
                }
            }
        }
        else {
            digitalWrite(m_ledPin, m_ledOff);
            if (wifi_conn_timestamp > 0L)  {
                wifi_lost_cnt++;
                wifi_conn_timestamp = 0L;
                wifi_lost_timestamp = time(nullptr);
                wifi_lost_status = wifiConnect.getStatus();
                fire_change_event(EVENT_WIFI_LOST);
                LOG("EVENT_WIFI_LOST: ", (ulong)wifi_lost_timestamp);
                if (mqtt_conn_timestamp > 0L) {
                    mqtt_lost_cnt++;
                    mqtt_lost_state = mqttClient.getState();
                    mqtt_conn_timestamp = 0L;
                    mqtt_lost_timestamp = wifi_lost_timestamp;
                    fire_change_event(EVENT_MQTT_LOST);
                    LOG("EVENT_MQTT_LOST: ", (ulong) mqtt_lost_timestamp);
                }
            }
        }
        fire_test_event();
    }
    if (t - m_keepaliveCheckTime >= m_keepaliveInterval) {
        m_keepaliveCheckTime = t;
        fire_keepalive_event();
    }
}


void ConnectionMonitor::debug_info(void) {
    time_t now = time(nullptr);
    Serial.println("\nConnectionMonitor.cpp:\n======================");
    Serial.printf("Now: %s\n", formatTime(now));
    Serial.printf("wifi_lost_timestamp: %u\n", (uint32_t)wifi_lost_timestamp);
    Serial.printf("mqtt_lost_timestamp: %u\n", (uint32_t)mqtt_lost_timestamp);
    Serial.printf("wifi_lost_cnt: %d\n", wifi_lost_cnt);
    Serial.printf("mqtt_lost_cnt: %d\n", mqtt_lost_cnt);
    Serial.printf("wifi_lost_status: %d\n", wifi_lost_status);
    Serial.printf("mqtt_lost_state: %d\n", mqtt_lost_state);
    Serial.printf("wifi_conn_timestamp: %u\n", (uint32_t)wifi_conn_timestamp);
    Serial.printf("mqtt_conn_timestamp: %u\n", (uint32_t)mqtt_conn_timestamp);
    if (wifi_lost_timestamp > 0L) {
        Serial.printf("wifi_lost: %s\n", formatTime(wifi_lost_timestamp));
    }
    if (mqtt_lost_timestamp > 0L) {
        Serial.printf("mqtt_lost: %s\n", formatTime(mqtt_lost_timestamp));
    }
    if (wifi_conn_timestamp > 0L) {
        Serial.printf("wifi_conn: %s\n", formatTime(wifi_conn_timestamp));
    }
    if (mqtt_conn_timestamp > 0L) {
        Serial.printf("mqtt_conn: %s\n", formatTime(mqtt_conn_timestamp));
    }
    Serial.printf("bad_dhcp_ip_assigned: %d\n", bad_dhcp_ip_assigned);
}
