#pragma once

// WiFi & MQTT connection monitoring, status indication, and recovery management
#define CONNECTION_MONITOR_INTERVAL 1000  // 1 sec
#define CONNECTION_MONITOR_IP_ASSIGNED_INTERVAL 60000  // 60 sec
#define CONNECTION_MONITOR_KEEP_ALIVE_DEFAULT_INTERVAL 60000  // 60 sec

#define EVENT_WIFI_RESTORED 1
#define EVENT_MQTT_RESTORED 2
#define EVENT_WIFI_LOST     3
#define EVENT_MQTT_LOST     4

#define LED_PULLUP     0
#define LED_PULLDOWN   1

#define MIN_TIMESTAMP 1640998800L  // Timestamp in sec. 1640998800  January 1, 2022 3:00:00 AM GMT+02:00

class ConnectionMonitor {

    public:
        ConnectionMonitor();

        void begin(uint8_t ledPin);
        void begin(uint8_t ledPin, bool ledConnection);
        void begin(uint8_t ledPin, void (*change_event_callback_ptr)(uint8_t event));
        void begin(uint8_t ledPin, void (*change_event_callback_ptr)(uint8_t event), bool ledConnection);
        void setChangeEventCallback(void (*change_event_callback_ptr)(uint8_t event));
        void setTestEventCallback(void (*test_event_callback_ptr)());
        void setKeepAliveEventCallback(void (*keepalive_event_callback_ptr)(), unsigned long interval = 60000L);
        void handleClient();

        bool wifiConnected();
        bool mqttConnected();

        void debug_info(void);

    private:
        uint8_t m_ledPin = 0;
        bool m_ledConnection;
        bool m_ledOn;
        bool m_ledOff;
        unsigned long m_connectionCheckTime = CONNECTION_MONITOR_INTERVAL * -2;
        unsigned long m_ipAssignedCheckTime = CONNECTION_MONITOR_IP_ASSIGNED_INTERVAL * -2;
        unsigned long m_keepaliveCheckTime = 0L;
        void check_connection();

        void (*m_changeEventCallback)(uint8_t event) = 0;
        void fire_change_event(uint8_t event);
        void (*m_testEventCallback)() = 0;
        void fire_test_event();
        void (*m_keepaliveCallback)() = 0;
        void fire_keepalive_event();
        unsigned long m_keepaliveInterval = CONNECTION_MONITOR_KEEP_ALIVE_DEFAULT_INTERVAL;

    public:
        unsigned int wifi_lost_cnt = 0;
        unsigned int mqtt_lost_cnt = 0;
        int wifi_lost_status = 0;
        int mqtt_lost_state = 0;
        time_t wifi_lost_timestamp = 0L;
        time_t mqtt_lost_timestamp = 0L;
        time_t wifi_conn_timestamp = 0L;
        time_t mqtt_conn_timestamp = 0L;
        unsigned int bad_dhcp_ip_assigned = 0;
};

extern ConnectionMonitor connectionMonitor;
