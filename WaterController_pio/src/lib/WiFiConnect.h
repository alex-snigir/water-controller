#pragma once

#include "IPAddress.h"

#ifdef ESP8266
// #include <ESP8266WiFiType.h>
// #include <ESP8266WiFiGeneric.h>
#endif

#ifdef ESP32
//#include <WiFiType.h>

// Cloned from ESP8266 <lwip/ip4_addr.h>
/** 255.255.255.255 */
#define IPADDR_NONE         ((u32_t)0xffffffffUL)
/** 127.0.0.1 */
#define IPADDR_LOOPBACK     ((u32_t)0x7f000001UL)
/** 0.0.0.0 */
#define IPADDR_ANY          ((u32_t)0x00000000UL)
/** 255.255.255.255 */
#define IPADDR_BROADCAST    ((u32_t)0xffffffffUL)
#endif


class WiFiConnect {

    public:
        WiFiConnect() = default;

        /* Set non-blocking mode for the begin() function. In non-blocking mode, the begin() function 
        will not wait for WiFi connection and will return immediately. The connection process will continue 
        in the background, and the connected() function can be used to check the connection status.*/
        void setNonBlocking(bool nb) { non_blocking = nb; }      // Call before begin() to set non-blocking mode

        /* Note: ArduinoOTA.begin() internally calls MDNS.begin(hostname) and registers the _arduino._tcp service on 
        both platforms. If MdnsClient or WiFiConnect with mdns_enabled=true then calls MDNS.begin() again, it reinitialises 
        the mDNS stack and loses the Arduino service registration, making OTA discovery fail.

        The correct split is:
        ┌─────────────────────────────────┬────────────────────────────────────────────────────────────────────────────────────────────────────────┐
        │            Scenario             │                                             Who owns mDNS                                              │
        ├─────────────────────────────────┼────────────────────────────────────────────────────────────────────────────────────────────────────────┤
        │ OTAClient used                  │ ArduinoOTA.begin() — do not call MDNS.begin() separately                                               │
        ├─────────────────────────────────┼────────────────────────────────────────────────────────────────────────────────────────────────────────┤
        │ No OTA, want mDNS for discovery │ WiFiConnect with mdns_enabled=true / MdnsClient                                                        │
        ├─────────────────────────────────┼────────────────────────────────────────────────────────────────────────────────────────────────────────┤
        │ Both                            │ ArduinoOTA.begin() owns it; add extra services via MDNS.addService() after, never re-call MDNS.begin() │
        └─────────────────────────────────┴────────────────────────────────────────────────────────────────────────────────────────────────────────┘ */
        void setMdnsEnabled(bool enabled) { mdns_enabled = enabled; }  // Call before begin() when not using ArduinoOTA

        bool begin(const char* ssid, const char* pwd,
                      const char*   hostname        = nullptr,
                      void        (*callback)(void) = nullptr);

        bool begin(const char* ssid, const char* pwd,
                      bool       generate_hostname,
                      void        (*callback)(void) = nullptr);

        bool connected();
        String getIP();
        String getHostname();
        String getMAC();
        int8_t getRSSI();
        int8_t getChannel();
        String getBSSID();
        String getSSID();
        String getNetworkID();
        IPAddress getNetworkIDAddress();
        String getGatewayIP();
        IPAddress getGatewayIPAddress();
        bool isIPAddressSet(IPAddress ip);

        void handleClient();
        void reconnect();
        void debug_info();
        void try_reconnect();
        void set_phy_11b();
        void set_phy_11g();
        void set_phy_11n();

        int getStatus();
        const char* getStatusString();
        const char* getStatusString(uint8_t status);
        const char* getModeString();
        const char* getModeString(uint8_t mode);
        uint8_t getSleepMode();
        const char* getSleepModeString();
        const char* getSleepModeString(uint8_t mode);
        uint8_t getPhyMode();
        const char* getPhyModeString();
        const char* getPhyModeString(uint8_t mode);
        uint8_t getStationStatus();
        const char* getStationStatusString();
        const char* getStationStatusString(uint8_t stationStatus);

    private:
        const char* ssid = nullptr;
        const char* pwd = nullptr;
        const char* hostname = nullptr;
        char desired_hostname[33] = {0};  // Own copy of the hostname requested in begin() (SDK limit is 32 chars).
                                          // Needed because the SDK resets its hostname global behind our back, and because
                                          // 'hostname' may point into that very global when generate_hostname is used.
        bool generate_hostname = false;
        bool mdns_enabled = false;   // Enable mDNS by WiFiClient. Not required in case OTA Client is used. (default false)
        bool non_blocking = false;   // Use non-blocking version of the begin() function, which does not wait for WiFi connection in the begin() function.
        bool initial_connection_finished = false;
        bool reconnection_in_progress = false;
        unsigned long last_reconnect_attempt = 0L;
        unsigned long test_try_N_lastActivity = 0L;
        void (*custom_callback)(void) = nullptr;
        uint8_t reconnect_attempt_count = 0;
        unsigned long test_connection_lastActivity = 0L;
        unsigned long test_connection_cb_lastActivity = 0L;

    private:
        bool begin_impl(const char* ssid, const char* pwd,
                           const char* hostname,
                           void      (*callback)(void),
                           bool     generate_hostname);
        void set_hostname();
        void keep_hostname_copy(const char* name);
        void restore_hostname();
        void connection_info();
        void initial_connection_setup();
        void set_event_handlers();
        void test_connection();
        IPAddress computeNetworkID();
        #ifdef ESP32
        void log_phy_error(esp_err_t err);
        #endif

        //WiFiEventHandler gotIpEventHandler;
        //WiFiEventHandler connectedEventHandler;
        //WiFiEventHandler disconnectedEventHandler;
        //WiFiEventHandler dhcpTimeoutEventHandler;

};

extern WiFiConnect wifiConnect;
