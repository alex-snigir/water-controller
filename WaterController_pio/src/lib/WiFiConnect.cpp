// WiFiConnect 
//
// A wrapper class for WiFi connection management, including connection setup, status monitoring, and reconnection handling.
// It supports both ESP8266 and ESP32 platforms, with features like non-blocking connection, mDNS support, and customizable event callbacks.
//
// Usage:
//   1. Singleton instance of WiFiConnect is created as wifiConnect. Include WiFiConnect.h and use wifiConnect in your main code.
//   2. For Non-blocking connection: Call wifiConnect.setNonBlocking(true) before begin() to enable non-blocking mode. 
//      In this mode, the begin() function will return immediately, and the connection process will continue in the background. 
//      Use wifiConnect.connected() to check the connection status.
//   3. For mDNS support: Call wifiConnect.setMdnsEnabled(true) before begin() to enable mDNS support in WiFiConnect. 
//      Note that if you are using ArduinoOTA, it already includes mDNS support, so do not call setMdnsEnabled(true) 
//      or MDNS.begin() separately in that case.
//   4. Call the begin() method with the desired parameters to initiate the WiFi connection.
//   5. Use the connected() method to check the connection status.
//   6. Call handleClient() in the main loop to manage connection events and reconnection logic.
//
// Note: The begin() method can be used in blocking or non-blocking mode. In non-blocking mode, the connection process 
//       will continue in the background, and the connected() function can be used to check the connection status. 
//       The class also provides various getters for connection information and status strings for debugging purposes.
//
// Features:
//     wifiConnect.setNonBlocking(true); // Set non-blocking mode for the begin() function, which will not wait for WiFi 
//                                       // connection and will return immediately. The connection process will continue in the background, 
//                                       // and the connected() function can be used to check the connection status.
//     wifiConnect.setNonBlocking(false); // Set blocking mode for the begin() function, which will wait for WiFi connection before returning. 
//                                        // This is the default behavior.
//
//     wifiConnect.setMdnsEnabled(true);  // Start mDNS responder in WiFiConnect (must not be used if using ArduinoOTA, which already includes mDNS support)
//     wifiConnect.setMdnsEnabled(false); // Do not start mDNS responder in WiFiConnect (default), do not call MDNS.begin() in WiFiConnect.
//
//     wifiConnect.begin(SSID, PWD, HOST);     // Use provided hostname (can be set to nullptr to use SDK default hostname)
//
//     wifiConnect.begin(SSID, PWD, true);     // true = generate hostname based on MAC address, e.g., ESP32-123456, ESP8266-123456, etc. 
//     wifiConnect.begin(SSID, PWD, false);    // (can be set to false to use SDK default hostname)
//                                             // Note: If hostname is generated, it can be retrieved later by getHostname() to use for MQTT and OTA clients.
//
//     wifiConnect.begin(SSID, PWD, HOST, callback);     // Use provided hostname and custom callback function (can be set to nullptr to use SDK default hostname)
//     wifiConnect.begin(SSID, PWD, true, callback);     // Generate hostname based on MAC address and use custom callback function
//
//     wifiConnect.connected(); // Check if WiFi is connected
//     wifiConnect.getIP(); // Get local IP address
//     wifiConnect.getHostname(); // Get WiFi hostname
//     wifiConnect.getMAC(); // Get MAC address
//     wifiConnect.getRSSI(); // Get WiFi signal strength (RSSI)
//     wifiConnect.getChannel(); // Get WiFi channel
//     wifiConnect.getBSSID(); // Get BSSID of the connected WiFi network
//     wifiConnect.getSSID(); // Get SSID of the connected WiFi network
//     wifiConnect.getNetworkID(); // Get network ID (SSID + BSSID)
//
// Version history:
//   v0.4          - ESP8266 & ESP32 supported
//   v0.5 20220712 - Host/MDNS fixes
//   v0.6 20221014 - Added debug_info()
//   v0.7 20221015 - None blocking connect 
//   v0.8 20221203 - Reconnect implemented, more info logs
//   v0.9 20221213 - Fixes: Added reconnect and initial_connection_setup() in the begin()
//   v0.10 20221219 - more data in debug_info()
//   v0.11 20221225 - set this->hostname when it was generated
//   v0.12 20230102 - Adding additional none-blocking begin's()
//   v0.13 20230403 - Adding additional getters
//   v0.14 20230501 - Verify this.hosthame is not null, getChannel() added. (ESP8266 Arduino Core 3.1.2 / ESP32 Arduino 2.0.8)
//   v0.15 20230502 - Refacroring. Remove try_reconnect() from set_phy_xxx
//   v0.16 20230519 - Added additional getters
//   v0.17 20230603 - MdnsClient, Reconnect policy changed, generate hostname fix for re-connect
//   v0.18 20230909 - Remove static member variables
//   v0.19 20231206 - Members initialization fix
//   v0.20 20251021 - Added pMode to log
//   v0.21 20260327 - Added getStatus()
//   v0.22 20260429 - Removed debug password; getStationStatus(), etc...
//   v0.23 20260607 - Refactoring, AI suggested improvements, etc...
//   v0.24 20260608 - Minor fixes
//   v0.25 20260611 - Replacing legacy WiFi events, IPv6 for different arduino frameworks
//   v0.26 20260921 - Restore the hostname after the SDK resets it on a PHY mode change (device was
//                    reporting "ESP-XXXXXX" after the 11N->11G fallback, until reboot);
//                    reset reconnect_attempt_count on a successful connection, so the PHY fallback
//                    no longer fires on accumulated attempts from earlier, unrelated outages.
//   v0.27 20260923 - Restore the hostname right after every PHY mode change (ESP8266), not only on
//                    reconnect: set_phy_11n() in begin() reset it when the SDK booted in another PHY
//                    mode, so setup() handed "ESP-XXXXXX" to OTA/mDNS and MQTT. Reconnect restore no
//                    longer waits for the 10 s poll.
//   v0.28 20261031 - Removed set_phy_lr(): LR alone cannot associate with a regular access point.

#ifdef ESP8266
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#endif
#ifdef ESP32
#include <WiFi.h>
// #include <WiFiType.h>
#include <ESPmDNS.h>
#include <esp_wifi.h>
#endif

#include "Log.h"
#include "WiFiConnect.h"
#include "MdnsClient.h"

// Test WiFi connection interval
#define TEST_CONNECTION_INTERVAL_INITIAL  500   // 0.5 sec
#define TEST_CONNECTION_INTERVAL_CALLBACK 500   // 0.5 sec
#define TEST_CONNECTION_INTERVAL_NB 10000       // 10 sec
#define RECONNECT_INTERVAL 60000                // 60 sec
#define TRY_PHY_N_INTERVAL 3600000              // 60 min


WiFiConnect wifiConnect;  // Created wifiConnect instance

bool WiFiConnect::begin(const char* ssid, const char* pwd,
                           const char* hostname,
                           void      (*callback)(void)) {
    return begin_impl(ssid, pwd, hostname, callback, false);
}

bool WiFiConnect::begin(const char* ssid, const char* pwd,
                           bool     generate_hostname,
                           void      (*callback)(void)) {
    return begin_impl(ssid, pwd, nullptr, callback, generate_hostname);
}

bool WiFiConnect::begin_impl(const char* ssid, const char* pwd, const char *hostname, void (*custom_callback)(void), bool generate_hostname) {

    this->ssid = ssid;
    this->pwd = pwd;
    this->hostname = hostname;
    this->generate_hostname = generate_hostname;
    this->custom_callback = custom_callback;
    this->initial_connection_finished = false;
    
    this->test_try_N_lastActivity = millis();

    this->reconnect_attempt_count = 0;
    this->test_connection_lastActivity = TEST_CONNECTION_INTERVAL_NB * -2; // 10000 * -2 = -20000 => 2³² - 20000 => 4294967296 - 20000 => 4294947296 ms until overflow, which is about 49.7 days, so it should be safe for our use case
    this->test_connection_cb_lastActivity = TEST_CONNECTION_INTERVAL_CALLBACK * -2; // 500 * -2 = -1000 => 2³² - 1000 => 4294966296 ms until overflow, which is about 49.7 days, so it should be safe for our use case

    WiFi.persistent(false);
    
    set_event_handlers();
    set_hostname(); // Set hostname before WiFi.begin() for ESP32, for ESP8266 it can be set at any point before WiFi.begin()

    WiFi.mode(WIFI_STA);
    set_phy_11n();
  
    WiFi.begin(ssid, pwd);        
    Serial.printf("\nWiFiConnect: Current mode: %s\n", getPhyModeString());
    LOG("WiFiConnect: Connecting to ", ssid, " ...");

    if (!non_blocking) {
        Serial.println("WiFiConnect: Blocking mode enabled, waiting for WiFi connection in begin()");
        last_reconnect_attempt = millis();
        while (WiFi.status() != WL_CONNECTED) { // Wait for the Wi-Fi to connect
            if (custom_callback != nullptr) {
                custom_callback();
            }
#ifdef ESP8266
            WiFi.setSleepMode(WIFI_NONE_SLEEP);
#endif
#ifdef ESP32
            WiFi.setSleep(WIFI_PS_NONE);
#endif
            unsigned long period = millis() - last_reconnect_attempt;
            bool reconnect_fl = (period >= RECONNECT_INTERVAL);
            if (reconnect_fl) {
                reconnect();
                last_reconnect_attempt = millis();
                Serial.printf("WiFiConnect: Try to re-connect WiFi AP. WiFi status: %d - %s\n", 
                WiFi.status(), getStatusString());
            }
            else {
                delay(TEST_CONNECTION_INTERVAL_INITIAL);
                LOGX(".");
            }
        }
        reconnect_attempt_count = 0;    // Connected - do not carry the initial attempts into the first outage
        restore_hostname();             // A PHY fallback in the loop above resets the SDK hostname
        initial_connection_setup();
        connection_info();
    }
    else {
        Serial.println("WiFiConnect: Non-blocking mode enabled, not waiting for WiFi connection in begin()");
    }

    return true;
}

void WiFiConnect::set_event_handlers() {
#ifdef ESP32
    // FreeRTOS Threading Alert: On the ESP32, your callback logic runs within a separate, background 
    // FreeRTOS task. Never use delay() inside your event-handling blocks, or you risk locking up the underlying Wi-Fi stack.
    // Global callback function executed by FreeRTOS background threads
    WiFi.onEvent([/*this*/](WiFiEvent_t event, WiFiEventInfo_t info) {
        switch (event) {
            case ARDUINO_EVENT_WIFI_STA_START: // ESP32 Station start
                Serial.println("\n### WiFi EVENT: Station start");
                break;            
            case ARDUINO_EVENT_WIFI_STA_CONNECTED: // ESP32 station connected to AP
                Serial.println("\n### WiFi EVENT: Station connected");
                break;
            case ARDUINO_EVENT_WIFI_STA_GOT_IP: // ESP32 station got IP from connected AP
                Serial.printf("\n### WiFi EVENT: Station got IP: %s\n", WiFi.localIP().toString().c_str());
                break;
            case ARDUINO_EVENT_WIFI_STA_LOST_IP: // ESP32 station lost IP and the IP is reset to 0
                Serial.println("\n### WiFi EVENT: Station lost IP");
                break;
            case ARDUINO_EVENT_WIFI_STA_DISCONNECTED: // ESP32 station disconnected from AP
                Serial.println("\n### WiFi EVENT: Station disconnected");
                break;
            default:
                Serial.printf("\n### WiFi EVENT: %d\n", event);
                break;
        }
    });
#endif
#ifdef ESP8266
    static WiFiEventHandler gotIpEventHandler, connectedEventHandler, disconnectedEventHandler, dhcpTimeoutEventHandler;

    connectedEventHandler = WiFi.onStationModeConnected ([/*this*/](const WiFiEventStationModeConnected &event) {
        Serial.println("### WiFi EVENT: Station connected");
        //Serial.printf("\n### Alex DEBUG: Station connected this->hostname: %s\n", this->hostname != NULL ? this->hostname : "NULL");
    });

    gotIpEventHandler = WiFi.onStationModeGotIP([/*this*/](const WiFiEventStationModeGotIP& event) {
        Serial.printf("### WiFi EVENT: Station got IP: %s\n", WiFi.localIP().toString().c_str());
        //Serial.printf("\n### Alex DEBUG: Station got IP this->hostname: %s\n", this->hostname != NULL ? this->hostname : "NULL");
    });

    disconnectedEventHandler = WiFi.onStationModeDisconnected([/*this*/](const WiFiEventStationModeDisconnected& event) {
        Serial.println("### WiFi EVENT: Station disconnected");
        //Serial.printf("\n### Alex DEBUG: Station disconnected this->hostname: %s\n", this->hostname != NULL ? this->hostname : "NULL");
    });

    dhcpTimeoutEventHandler = WiFi.onStationModeDHCPTimeout ([/*this*/](void) {
        Serial.println("### WiFi EVENT: DHCP Timeout");
        //Serial.printf("\n### Alex DEBUG: DHCP Timeout this->hostname: %s\n", this->hostname != NULL ? this->hostname : "NULL");
    });
#endif
}

/*
    ESP32:   hostname to be set before calling WiFi.mode(), WiFi.config(), or WiFi.begin()
    ESP8266: hostname to be set at any point before initiating the connection via WiFi.begin()
*/
void WiFiConnect::set_hostname() {
#ifdef ESP32
    // ESP32: hostname must be set before WiFi.begin()
    if (generate_hostname) {
        char _hostname[13];
        uint8_t mac[6];
        WiFi.macAddress(mac);
        sprintf(_hostname, "ESP32-%02X%02X%02X", mac[3], mac[4], mac[5]);
        Serial.printf("Set generated hostname: %s\n", _hostname);
        WiFi.setHostname(_hostname);
        this->hostname = WiFi.getHostname();
        keep_hostname_copy(_hostname);
    } else if (this->hostname != nullptr) {
        Serial.printf("Set provided hostname: %s\n", this->hostname);
        WiFi.setHostname(this->hostname);
        keep_hostname_copy(this->hostname);
    } else {
        // else: leave SDK default (esp32-AABBCC)
        // The ESP-IDF hasn't yet written the MAC into the default hostname and it prints esp32-000000, 
        // so do not use WiFi.getHostname() here as it will initialize the WiFi and set the hostname to the default one, which is not what we want.
        Serial.printf("WiFiConnect: No hostname provided, using SDK default hostname");
    }
#endif
#ifdef ESP8266
    // ESP8266: hostname can be set at any point before WiFi.begin()
    if (generate_hostname) {
        char _hostname[15];
        uint8_t mac[6];
        WiFi.macAddress(mac);
        sprintf(_hostname, "ESP8266-%02X%02X%02X", mac[3], mac[4], mac[5]);
        Serial.printf("Set generated hostname: %s\n", _hostname);
        WiFi.setHostname(_hostname);
        this->hostname = WiFi.getHostname();
        keep_hostname_copy(_hostname);
    } else if (this->hostname != nullptr) {
        Serial.printf("Set provided hostname: %s\n", this->hostname);
        WiFi.setHostname(this->hostname);
        keep_hostname_copy(this->hostname);
    } else {
        // else: leave SDK default (ESP-AABBCC)
        Serial.printf("No hostname provided, using SDK default hostname: %s\n", WiFi.getHostname());
    }
#endif
}


/*
    Store our own copy of the hostname requested in begin().

    Two reasons not to rely on this->hostname for restoring it later:
      - with generate_hostname, this->hostname is assigned WiFi.getHostname(), which on ESP8266
        points straight into the SDK's own hostname global, so it changes when the SDK resets it;
      - the caller's buffer is not guaranteed to outlive begin().
*/
void WiFiConnect::keep_hostname_copy(const char* name) {
    if (name == nullptr) {
        desired_hostname[0] = '\0';
        return;
    }
    strncpy(desired_hostname, name, sizeof(desired_hostname) - 1);
    desired_hostname[sizeof(desired_hostname) - 1] = '\0';
}


/*
    Re-apply the hostname requested in begin(), if the SDK has dropped it.

    The ESP8266 SDK keeps the station hostname in a single global buffer (wifi_station_hostname,
    defined in eagle_lwip_if.o). That buffer is re-initialised to the factory default
    "ESP-<last 3 MAC bytes>" every time the station netif is re-allocated, and a PHY mode change
    does exactly that - see the 11N<->11G fallback in reconnect(). The name set once in begin()
    was therefore lost on the first PHY switch and never came back until the next reboot.
    The failure is easy to misread, because mDNS/OTA keep working: ArduinoOTA captured the name
    separately at begin(), so "mdns" stays correct while "hostname" silently goes default.
    (See also the comment in the core's cores/esp8266/LwipIntf.cpp about the SDK overwriting it.)

    Called whenever a connection is (re)established. It writes only when the value actually drifted;
    WiFi.setHostname() also triggers dhcp_renew(), so the DHCP server picks the name up again.
*/
void WiFiConnect::restore_hostname() {
    if (desired_hostname[0] == '\0') {
        return;     // No hostname was requested - the SDK default is intentional here
    }
    const char* current = WiFi.getHostname();
    if (current != nullptr && strcmp(current, desired_hostname) == 0) {
        return;     // Still what we asked for
    }
    LOG_ERROR("WiFiConnect: Hostname was reset by the SDK to: ", current == nullptr ? "(null)" : current);
    LOG("WiFiConnect: Restoring hostname: ", desired_hostname);
    WiFi.setHostname(desired_hostname);
}


void WiFiConnect::connection_info()
{
    if (WiFi.status() == WL_CONNECTED) {
        LOG("\nConnection established!");
        LOG("Connected to: ", WiFi.SSID());
        LOG("IP address: ", WiFi.localIP().toString().c_str()); // .c_str()
        LOG("WiFi Hostname: ", getHostname());
        LOG("AutoReconnect: ", WiFi.getAutoReconnect());
        LOG("Phy Mode: ", getPhyModeString());
        LOG("Channel: ", WiFi.channel());
        LOG("RSSI: ", WiFi.RSSI());
        LOG("MAC address: ", WiFi.macAddress());
        LOG("BSSID: ", WiFi.BSSIDstr());
        LOG("Subnet Mask: ", WiFi.subnetMask().toString().c_str());
        LOG("Gateway IP: ", WiFi.gatewayIP().toString().c_str());
        LOG("DNS IP: ", WiFi.dnsIP().toString().c_str());
    }
}

/*
    Run additional setup after initial connection is established, e.g., mDNS responder, etc...
    Note: This is called from the begin() after the initial connection is established, 
    and also can be called from the handleClient() when established first time.
    if the connection was lost and re-established it should not be called again.
*/
void WiFiConnect::initial_connection_setup() {
    if (!initial_connection_finished) {
        if (WiFi.status() == WL_CONNECTED) {
            if (mdns_enabled) {
                if (!MDNS.begin(getHostname().c_str())) { // Start the mDNS responder for <host>.local
                    LOG_ERROR("Error setting up mDNS responder for ", getHostname().c_str(), ".local");
                }
                else {
                    LOG("mDNS responder started: ", MDNS.hostname(0) + ".local");
                    // Add service to MDNS-SD (optional, e.g., HTTP server on port 80)
                    // MDNS.addService("http", "tcp", 80);
                }
            }
            initial_connection_finished = true;
        }
    }
}


void WiFiConnect::reconnect() 
{
    bool phy11n;

#ifdef ESP8266
    phy11n = (getPhyMode() != WIFI_PHY_MODE_11N) ? false : true;
#endif
#ifdef ESP32
    phy11n = ((getPhyMode()&WIFI_PROTOCOL_11N) != WIFI_PROTOCOL_11N) ? false : true;
#endif

    if (phy11n && reconnect_attempt_count == 3) {   // Try it 3+1 times on 11N before entering here
        LOG("WiFiConnect: Setting WIFI_PHY_MODE_11G");
        set_phy_11g();
        try_reconnect();
        //phy11n = false;
        reconnect_attempt_count = 0;
        return;
    }
    if (!phy11n && reconnect_attempt_count == 2) {   // Try it 2+1 times on 11G before entering here
        LOG("WiFiConnect: Setting WIFI_PHY_MODE_11N");
        set_phy_11n();
        try_reconnect();
        //phy11n = true;
        reconnect_attempt_count = 0;
        return;
    }
    reconnect_attempt_count++;
    Serial.print("\nWiFiConnect: Using ");
    Serial.print(getPhyModeString());
    Serial.print(" attempt: ");
    Serial.println(reconnect_attempt_count);
    WiFi.reconnect();
}


bool WiFiConnect::connected() {
    return (WiFi.status() == WL_CONNECTED);
}


String WiFiConnect::getIP() {
    // LOG("IP address: ", WiFi.localIP().toString().c_str());
    return WiFi.localIP().toString();
}


String WiFiConnect::getHostname() {
#ifdef ESP8266
    return WiFi.hostname();
#endif
#ifdef ESP32
    return WiFi.getHostname();
#endif
}


String WiFiConnect::getMAC() {
    return WiFi.macAddress();
}


int8_t WiFiConnect::getRSSI() {
    return WiFi.RSSI();
}


int8_t WiFiConnect::getChannel() {
    return WiFi.channel();
}

String WiFiConnect::getSSID() {
    return WiFi.SSID();
}


String WiFiConnect::getBSSID() {
    return WiFi.BSSIDstr();
}


void WiFiConnect::handleClient() {
    #ifdef ESP8266
    if (mdns_enabled) {
        // Note: On ESP8266, you must include MDNS.update(); inside your main loop() function. 
        // The ESP32 framework processes mDNS packets using background system threads natively, 
        // making manual loops unnecessary on that chip.
        MDNS.update();
    }
    #endif

    unsigned long t = millis();
    if (reconnection_in_progress && WiFi.status() == WL_CONNECTED) {
        // Do not wait for the 10 s poll below: MQTT can reconnect (and publish connection info) inside that window
        restore_hostname();
    }
    if (reconnection_in_progress) {
        unsigned long period = t - test_connection_lastActivity;
        if (period >= TEST_CONNECTION_INTERVAL_NB) {
            test_connection_lastActivity = t;
            if (WiFi.status() == WL_CONNECTED) {
                reconnection_in_progress = false;
                reconnect_attempt_count = 0;    // This outage is over - do not carry its attempts into the next one
                restore_hostname();             // A PHY fallback during this outage resets the SDK hostname
                if (!initial_connection_finished) {
                    initial_connection_setup();
                }
                connection_info();
            }
            else {
                // if (custom_callback != nullptr) {
                //   custom_callback();
                // }
                #ifdef ESP8266
                WiFi.setSleepMode(WIFI_NONE_SLEEP);
                #endif
                #ifdef ESP32
                WiFi.setSleep(WIFI_PS_NONE);
                #endif
                period = t - last_reconnect_attempt;
                bool reconnect_fl = period >= RECONNECT_INTERVAL;
                if (reconnect_fl)  {
                    Serial.printf("\nNB: WiFiConnect: Current mode: %s\n", getPhyModeString());
                    reconnect();
                    last_reconnect_attempt = t;
                }
                Serial.printf("NB: WiFiConnect: Try to %sconnect WiFi AP. WiFi status: %d - %s\n",
                    reconnect_fl ? "re-" : "", WiFi.status(), getStatusString());
            }
        }
        period = t - test_connection_cb_lastActivity;
        if (period >= TEST_CONNECTION_INTERVAL_CALLBACK) {
            test_connection_cb_lastActivity = t;
            if (custom_callback != nullptr) {
                custom_callback();
            }
        }
    }
    else {
        if (WiFi.status() != WL_CONNECTED) {
            try_reconnect();
        }
        else {
            unsigned long period = t - test_try_N_lastActivity;
            if (period >= TRY_PHY_N_INTERVAL) {
                #ifdef ESP8266
                if (getPhyMode() != WIFI_PHY_MODE_11N) {
                #endif
                #ifdef ESP32
                if ((getPhyMode() & WIFI_PROTOCOL_11N) != WIFI_PROTOCOL_11N) {
                #endif
                    Serial.printf("NB: WiFiConnect: Try to re-connect WiFi AP to 11N. Current WiFi status: %d - %s, PhyMode: %d - %s\n",
                                    WiFi.status(), getStatusString(), getPhyMode(), getPhyModeString());
                    set_phy_11n();
                    try_reconnect();
                    test_try_N_lastActivity = t;
                }
            }
        }
    }
}


/*
typedef enum {
    WL_NO_SHIELD        = 255,   // for compatibility with WiFi Shield library
    WL_IDLE_STATUS      = 0,
    WL_NO_SSID_AVAIL    = 1,
    WL_SCAN_COMPLETED   = 2,
    WL_CONNECTED        = 3,
    WL_CONNECT_FAILED   = 4,
    WL_CONNECTION_LOST  = 5,
    WL_DISCONNECTED     = 6
} wl_status_t;
*/


int WiFiConnect::getStatus()
{
    return WiFi.status();
}


const char *WiFiConnect::getStatusString()
{
    return getStatusString(WiFi.status());
}


const char *WiFiConnect::getStatusString(uint8_t status)
{
    switch (status)
    {
    case WL_NO_SHIELD:
        return "WL_NO_SHIELD";
    case WL_IDLE_STATUS:
        return "WL_IDLE_STATUS";
    case WL_NO_SSID_AVAIL:
        return "WL_NO_SSID_AVAIL";
    case WL_SCAN_COMPLETED:
        return "WL_SCAN_COMPLETED";
    case WL_CONNECTED:
        return "WL_CONNECTED";
    case WL_CONNECT_FAILED:
        return "WL_CONNECT_FAILED";
    case WL_CONNECTION_LOST:
        return "WL_CONNECTION_LOST";
    case WL_DISCONNECTED:
        return "WL_DISCONNECTED";
    default:
        return "UNKNOWN";
    }
}


const char *WiFiConnect::getModeString()
{
    return getModeString(WiFi.getMode());
}


const char *WiFiConnect::getModeString(uint8_t mode)
{
    switch (mode) {
    case WIFI_OFF:
        return "WIFI_OFF";
    case WIFI_STA:
        return "WIFI_STA";
    case WIFI_AP:
        return "WIFI_AP";
    case WIFI_AP_STA:
        return "WIFI_AP_STA";
    default:
        return "UNKNOWN";
    }
}


uint8_t WiFiConnect::getSleepMode()
{
#ifdef ESP8266
    return WiFi.getSleepMode();
#endif
#ifdef ESP32
    return WiFi.getSleep();
#endif
}


const char *WiFiConnect::getSleepModeString()
{
    return getSleepModeString(getSleepMode());
}


const char *WiFiConnect::getSleepModeString(uint8_t mode)
{
    switch (mode)
    {
#ifdef ESP8266
    case WIFI_NONE_SLEEP:
        return "WIFI_NONE_SLEEP";
    case WIFI_LIGHT_SLEEP:
        return "WIFI_LIGHT_SLEEP";
    case WIFI_MODEM_SLEEP:
        return "WIFI_MODEM_SLEEP";
#endif
#ifdef ESP32
    case WIFI_PS_NONE: // No power save
        return "WIFI_PS_NONE";
    case WIFI_PS_MIN_MODEM: // Minimum modem power saving. In this mode, station wakes up to receive beacon every DTIM period
        return "WIFI_PS_MIN_MODEM";
    case WIFI_PS_MAX_MODEM: // Maximum modem power saving. In this mode, interval to receive beacons is determined by the listen_interval parameter in wifi_sta_config_t
        return "WIFI_PS_MAX_MODEM";
#endif
    default:
        return "UNKNOWN";
    }
}


uint8_t WiFiConnect::getPhyMode()
{
#ifdef ESP8266
    return WiFi.getPhyMode();
#endif
#ifdef ESP32
    uint8_t protocol_bitmap = 0;
    esp_err_t error_code = esp_wifi_get_protocol((wifi_interface_t)ESP_IF_WIFI_STA, &protocol_bitmap);
    if (error_code != 0)
    {
        char buf[100];
        buf[0] = 0;
        esp_err_to_name_r(error_code, buf, 100);
        Serial.print("esp_wifi_get_protocol error code: ");
        Serial.print(error_code);
        Serial.print(" : ");
        Serial.println(buf);
        return 0;
    }
    else
    {
        // Serial.print("Current protocol bitmap is ");
        // Serial.println(protocol_bitmap);
        return protocol_bitmap;
    }
#endif
}

const char *WiFiConnect::getPhyModeString()
{
    return getPhyModeString(getPhyMode());
}

const char *WiFiConnect::getPhyModeString(uint8_t mode)
{
#ifdef ESP8266
    switch(mode) {
    case WIFI_PHY_MODE_11B:
        return "WIFI_PHY_MODE_11B";
    case WIFI_PHY_MODE_11G:
        return "WIFI_PHY_MODE_11G";
    case WIFI_PHY_MODE_11N:
        return "WIFI_PHY_MODE_11N";
    default:
        return "UNKNOWN";
    }
#endif
#ifdef ESP32
    if ((mode&WIFI_PROTOCOL_11N) == WIFI_PROTOCOL_11N && (mode&WIFI_PROTOCOL_11G) == WIFI_PROTOCOL_11G && (mode&WIFI_PROTOCOL_11B) == WIFI_PROTOCOL_11B)
        return "11B|11G|11N";
    if ((mode&WIFI_PROTOCOL_11G) == WIFI_PROTOCOL_11G && (mode&WIFI_PROTOCOL_11B) == WIFI_PROTOCOL_11B)
        return "11B|11G";
    if ((mode&WIFI_PROTOCOL_11B) == WIFI_PROTOCOL_11B)
        return "11B";
    return "not decoded";
#endif
}


uint8_t WiFiConnect::getStationStatus() {
    #ifdef ESP8266
    return wifi_station_get_connect_status();
    #endif
    #ifdef ESP32
    return 0;
    #endif
}


const char* WiFiConnect::getStationStatusString() {
    return getStationStatusString(getStationStatus());
}


const char* WiFiConnect::getStationStatusString(uint8_t stationStatus) {
#ifdef ESP8266
    switch(stationStatus) {
    case STATION_IDLE:
        return "STATION_IDLE";
    case STATION_CONNECTING:
        return "STATION_CONNECTING";
    case STATION_WRONG_PASSWORD:
        return "STATION_WRONG_PASSWORD";
    case STATION_NO_AP_FOUND:
        return "STATION_NO_AP_FOUND";
    case STATION_CONNECT_FAIL:
        return "STATION_CONNECT_FAIL";
    case STATION_GOT_IP:
        return "STATION_GOT_IP";
    default:
        return "UNKNOWN";
    }
#endif
#ifdef ESP32
    return "N/A";
#endif
}


String WiFiConnect::getGatewayIP() {
    return WiFi.gatewayIP().toString();
}


IPAddress WiFiConnect::getGatewayIPAddress() {
    return WiFi.gatewayIP();
}


String WiFiConnect::getNetworkID() {
    #ifdef ESP32
    return  WiFi.networkID().toString();
    #endif
    #ifdef ESP8266
    return computeNetworkID().toString();
    #endif
}


IPAddress WiFiConnect::getNetworkIDAddress() {
    #ifdef ESP32
    return  WiFi.networkID();
    #endif
    #ifdef ESP8266
    return  computeNetworkID();
    #endif
}

IPAddress WiFiConnect::computeNetworkID() {
    IPAddress networkID;
    IPAddress gatewayIp = WiFi.gatewayIP();
    IPAddress subnet = WiFi.subnetMask();
    for (size_t i = 0; i < 4; i++) {
        networkID[i] = subnet[i] & gatewayIp[i];
    }
    return  networkID;
}

bool WiFiConnect::isIPAddressSet(IPAddress ip) {
    #ifdef ESP32
    return  (uint32_t)ip != IPADDR_NONE && (uint32_t)ip != IPADDR_ANY;
    #endif

    #ifdef ESP8266
    return ip.isSet();
    #endif
}


void WiFiConnect::debug_info() {
    Serial.println("\nWiFiConnect.cpp:\n================");
    Serial.printf("WiFi status: %d - %s\n", WiFi.status(), getStatusString());
    #ifdef ESP8266
    Serial.printf("Station status: %d - %s\n", getStationStatus(), getStationStatusString());
    Serial.printf("AutoConnect: %d\n", WiFi.getAutoConnect());
    #endif
    Serial.printf("AutoReconnect: %d\n", WiFi.getAutoReconnect());
    Serial.printf("Mode: %d - %s\n", WiFi.getMode(), getModeString());
    Serial.printf("Sleep Mode: %d - %s\n", getSleepMode(), getSleepModeString());
    Serial.printf("Phy Mode: %d - %s\n", getPhyMode(), getPhyModeString());
    Serial.printf("RSSI: %d\n", WiFi.RSSI());
    Serial.printf("Channel: %d\n", WiFi.channel());
    #ifdef ESP8266
    Serial.printf("ListenInterval: %d\n", WiFi.getListenInterval());
    #endif
    Serial.printf("AP SSID: %s\n", WiFi.SSID().c_str());
    uint8_t *bssid = WiFi.BSSID(); // May be NULL when not associated with an AP
    if (bssid != nullptr) {
        Serial.printf("BSSID: %02X:%02X:%02X:%02X:%02X:%02X\n",
                                    bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5]);
    }
    else {
        Serial.printf("BSSID: not associated\n");
    }
    Serial.printf("MAC: %s\n", WiFi.macAddress().c_str()); 
    Serial.printf("IP: %s\n", WiFi.localIP().toString().c_str());
    // IPAddress ip= WiFi.localIP();
    // Serial.printf("IP: %d.%d.%d.%d\n", ip[0], ip[1], ip[2], ip[3]);
    Serial.printf("DNS IP0: %s\n", WiFi.dnsIP().toString().c_str());
    Serial.printf("DNS IP1: %s\n", WiFi.dnsIP(1).toString().c_str());
    Serial.printf("Gateway IP: %s\n", WiFi.gatewayIP().toString().c_str());
    Serial.printf("Subnet Mask: %s\n", WiFi.subnetMask().toString().c_str());
    Serial.printf("Broadcast IP: %s\n", WiFi.broadcastIP().toString().c_str());
    Serial.printf("NetworkID: %s\n", getNetworkID().c_str());
    #ifdef ESP32
    #if ESP_ARDUINO_VERSION < ESP_ARDUINO_VERSION_VAL(3, 0, 0)
    Serial.printf("IP V6: %s\n", WiFi.localIPv6().toString().c_str());
    #else
    Serial.printf("IP V6: %s\n", WiFi.linkLocalIPv6().c_str()); // Required in >= 3.x
    #endif
    #endif
    Serial.printf("WiFi Hostname: %s\n", getHostname().c_str());
    Serial.printf("Module initial_connection_finished: %s\n", initial_connection_finished ? "true" : "false");
    Serial.printf("Module reconnection_in_progress: %s\n", reconnection_in_progress ? "true" : "false");
    Serial.printf("Now: %lu millis since boot\n", millis());
    Serial.printf("Module test_try_N_lastActivity: %lu\n", test_try_N_lastActivity);
    Serial.printf("Module test_connection_cb_lastActivity: %lu\n", test_connection_cb_lastActivity);
    Serial.printf("Module last_reconnect_attempt: %lu\n", last_reconnect_attempt);
    Serial.printf("Module test_connection_lastActivity: %lu\n", test_connection_lastActivity);
    Serial.printf("Module hostname: %s\n", hostname != NULL ? hostname : "NULL");
    Serial.printf("Module generate_hostname: %s\n", generate_hostname ? "true" : "false");
    Serial.printf("Module mdns_enabled: %s\n", mdns_enabled ? "true" : "false");
    Serial.printf("Module non_blocking: %s\n", non_blocking ? "true" : "false");
    //WiFi.printDiag(Serial); // Note: Do not use the printDiag(). It prints WiFi password
    if (mdns_enabled) {
        mdnsClient.debug_info();
    }
}


void WiFiConnect::try_reconnect() {
    reconnection_in_progress = true;
    last_reconnect_attempt = millis();
    test_try_N_lastActivity = last_reconnect_attempt;
    WiFi.reconnect();
}


// Only set phy mode, do not call reconect in this function
void WiFiConnect::set_phy_11b() {
    #ifdef ESP8266
    WiFi.setPhyMode(WIFI_PHY_MODE_11B);
    restore_hostname();     // A real PHY mode change resets the SDK hostname
    #endif
    #ifdef ESP32
    esp_err_t error_code = esp_wifi_set_protocol((wifi_interface_t)ESP_IF_WIFI_STA,WIFI_PROTOCOL_11B);
    if (error_code != 0) {
        log_phy_error(error_code);
        return;
    }
    #endif
    // try_reconnect(); - do not call reconect in this function
}


// Only set phy mode, do not call reconect in this function
void WiFiConnect::set_phy_11g() {
    #ifdef ESP8266
    WiFi.setPhyMode(WIFI_PHY_MODE_11G);
    restore_hostname();     // A real PHY mode change resets the SDK hostname
    #endif
    #ifdef ESP32
    esp_err_t error_code = esp_wifi_set_protocol((wifi_interface_t)ESP_IF_WIFI_STA, WIFI_PROTOCOL_11G|WIFI_PROTOCOL_11B);
    if (error_code != 0) {
        log_phy_error(error_code);
        return;
    }
    #endif
    // try_reconnect(); - do not call reconnect in this function
}


// Only set phy mode, do not call reconnect in this function
void WiFiConnect::set_phy_11n() {
    #ifdef ESP8266
    WiFi.setPhyMode(WIFI_PHY_MODE_11N);
    restore_hostname();     // A real PHY mode change resets the SDK hostname
    #endif
    #ifdef ESP32
    esp_err_t error_code = esp_wifi_set_protocol((wifi_interface_t)ESP_IF_WIFI_STA, WIFI_PROTOCOL_11N|WIFI_PROTOCOL_11G|WIFI_PROTOCOL_11B);
    if (error_code != 0) {
        log_phy_error(error_code);
        return;
    }
    #endif
    // try_reconnect(); - do not call reconnect in this function
}


#ifdef ESP32
void WiFiConnect::log_phy_error(esp_err_t err) {
    char buf[100];
    buf[0] = 0;
    esp_err_to_name_r(err, buf, 100);
    Serial.print("esp_wifi_set_protocol error code: ");
    Serial.print(err);
    Serial.print(" : ");
    Serial.println(buf);
}
#endif