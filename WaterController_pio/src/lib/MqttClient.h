#pragma once

#define MQTT_MAX_PACKET_SIZE 2048
#define WILLQOS 0

#include <WiFiClient.h>
#include <PubSubClient.h>

class MqttClient {

    public:
        MqttClient();
        ~MqttClient();

        void setTimeOut(long timeout);
        void setReconnectInterval(long ms);

        void setAnnouncementTopic(const char *);
        void setLastWillTopic(const char *);
        void setLastWillTopic(const char *, bool);
        void setLastWillTopic(const char *, bool, const char *);
        void setTopics(const char **, int);
        void setClearRetainedTopics(const char **, int); // Command topics: retained messages deleted on connect, before subscribing
        void setCallback(void (*custom_callback)(char*, uint8_t*, unsigned int));
        void setConnectEvent(void (*connect_event)(bool connected));

        void begin(const char *mqtt_server, const char* clientId);
        void begin(const char *mqtt_server, const char* clientId, bool enabled);
        void begin(const char *mqtt_server, const char* clientId, const char *user, const char *passwd);
        
        bool connected();

        void handleClient();

        bool subscribe(const char* topic);
        bool publish(const char* topic, const char* payload);
        bool publish(const char* topic, const char* payload, bool retained);

        const char* getClientId();
        const char* getUser();
        const char* getBroker();
        int getState();

        void debug_info();

    private:
        WiFiClient m_wifiClient;
        PubSubClient m_pubSubClient;

        const char* m_announcementTopic = nullptr;
        const char* m_lastWillTopic = nullptr;
        bool m_lastWillRetain = false;
        const char* m_lastWillMessage = nullptr;

        const char** m_topics = nullptr;
        int m_topics_cnt = 0;
        const char** m_clearRetainedTopics = nullptr;
        int m_clearRetainedTopics_cnt = 0;

        bool m_enabled = false;
        const char* m_clientId = nullptr;
        const char* m_user = nullptr;
        const char* m_passwd = nullptr;
        const char* m_mqtt_server = nullptr;
        unsigned int m_mqtt_server_port = 1883;

        unsigned long m_lastReconnectAttempt = 0;
        unsigned long m_reconnectInterval = 10000;

        int m_lastState = 100; // Initial dummy value
        bool m_inLoop = false; // Reentrancy guard for the loop() call inside publish()

        void begin_impl(const char *mqtt_server, const char* clientId, const char *user, const char *passwd);
        bool reconnect();
        void publish_announcement();
        void clear_retained();
        void resubscribe();
        void callback(char* topic, byte* payload, unsigned int length);
        void (*custom_callback)(char*, uint8_t*, unsigned int) = 0;

        void fire_connect_event(bool connected);
        void (*connect_event)(bool connected) = 0;
};

extern MqttClient mqttClient;
