// MqttClient
// 
// v0.4 20220503 ESP8266 & ESP32 supported
// v0.5 20220608 Fire connect event
// v0.6 20220613 Last Will Testament support
// v0.7 20220614 Last Will Testament support fix
// v0.8 20221014 Test WiFi connection when reconnect, added debug_info()
// v0.9 20221202 Small fixes
// v0.10 20230403 Additional getters
// v0.11 20230501 Debug info added
// v0.12 20230602 publish() add sent/not sent to message
// v0.13 20231206 removing members initialization defects
// v0.14 20251021 _wifiClient->setNoDelay(true); 
// v0.15 20260328 MQTT state change logging; Added comments
// v0.16 20260428 Don't show password
// v0.17 20260607 Refactoring, AI suggested improvements, etc...
// v0.18 20260608 minor fix, MQTT_MAX_PACKET_SIZE 2048
// v0.19 20260610 publish() - process incoming MQTT events immediately after publish for maximum stability
// v0.20 20260922 WiFiClient timeout 1 s on ESP8266 and ESP32 (ESP32 2.x takes seconds - 1000 meant 1000 s),
//                PubSubClient socket timeout 3 s, reconnect interval counted from the end of the attempt,
//                WiFi down no longer consumes the reconnect interval (reconnect right after WiFi is back)
// v0.21 20261109 setClearRetainedTopics(): on connect, before subscribing, an empty retained message
//                deletes a command retained on the broker by mistake (PubSubClient does not pass the retain flag)
//
// TODO:
//   > Replace Serial.print() with LOG()
//
// Info:
//  Max length of the message <= MQTT_MAX_PACKET_SIZE
//  The length of the message = 5 (header len) + 2 (topic len) + strlen(topic) + strlen(payload)

#if defined(ESP8266)
#include <ESP8266WiFi.h>
#endif
#if defined(ESP32)
#include <WiFi.h>
#endif
#include "MqttClient.h"
#include "Log.h"

#define STR_CONNECTED    "connected"
#define STR_DISCONNECTED "disconnected"

#define MQTT_CLIENT_SOCKET_TIMEOUT_SECONDS 3

MqttClient mqttClient;  // Created MqttClient instance

MqttClient::MqttClient() {
    //_wifiClient = new WiFiClient();
    // WiFiClient::setTimeout() units differ per platform. It also bounds the TCP connect() in reconnect().
#if defined(ESP8266)
#define ESP8266_WIFI_CLIENT_TIMEOUT_MILLIS 1000
    m_wifiClient.setTimeout(ESP8266_WIFI_CLIENT_TIMEOUT_MILLIS);
#endif
#if defined(ESP32)
#define ESP32_WIFI_CLIENT_TIMEOUT_SECONDS 1    // Arduino-ESP32 2.x takes SECONDS here (changes to ms in 3.x)
    m_wifiClient.setTimeout(ESP32_WIFI_CLIENT_TIMEOUT_SECONDS);
#endif
    m_pubSubClient.setClient(m_wifiClient);
    m_pubSubClient.setBufferSize(MQTT_MAX_PACKET_SIZE);
    m_pubSubClient.setSocketTimeout(MQTT_CLIENT_SOCKET_TIMEOUT_SECONDS); // CONNACK/packet wait, library default is 15 s busy-wait
    m_lastWillTopic = nullptr;
    m_lastWillRetain = false;
    m_lastWillMessage = STR_DISCONNECTED;
}

MqttClient::~MqttClient() {
    if (m_pubSubClient.connected()) {
        m_pubSubClient.disconnect(); // clean disconnect — suppresses Last Will publication
    }
    free((void*)m_clientId);
}


void MqttClient::setTimeOut(long timeout) {
    m_wifiClient.setTimeout(timeout);
}


void MqttClient::setReconnectInterval(long reconnectInterval) {
    m_reconnectInterval = reconnectInterval;
}


void MqttClient::setAnnouncementTopic(const char *topic) {
    m_announcementTopic = topic;
}


void MqttClient::setLastWillTopic(const char *topic) {
    m_lastWillTopic   = topic;
    m_lastWillRetain  = false;
    m_lastWillMessage = STR_DISCONNECTED;
}


void MqttClient::setLastWillTopic(const char *topic, bool retain) {
    m_lastWillTopic   = topic;
    m_lastWillRetain  = retain;
    m_lastWillMessage = STR_DISCONNECTED;
}


void MqttClient::setLastWillTopic(const char *topic, bool retain, const char *message) {
    m_lastWillTopic   = topic;
    m_lastWillRetain  = retain;
    m_lastWillMessage = message;
}


void MqttClient::setTopics(const char **topics, int count) {
    m_topics = topics;
    m_topics_cnt = count;
}


void MqttClient::setClearRetainedTopics(const char **topics, int count) {
    m_clearRetainedTopics = topics;
    m_clearRetainedTopics_cnt = count;
}


void MqttClient::setCallback(void (*custom_callback)(char*, uint8_t*, unsigned int)) {
    this->custom_callback = custom_callback;
}


void MqttClient::setConnectEvent(void (*connect_event)(bool connected)) {
    this->connect_event = connect_event;
}


void MqttClient::begin(const char *mqtt_server, const char* clientId) {
    m_enabled = true;
    begin_impl(mqtt_server, clientId, nullptr, nullptr);
}


void MqttClient::begin(const char *mqtt_server, const char* clientId, bool enabled) {
    m_enabled = enabled;
    begin_impl(mqtt_server, clientId, nullptr, nullptr);
}


void MqttClient::begin(const char *mqtt_server, const char* clientId, const char *user, const char *passwd) {
    m_enabled = true;
    begin_impl(mqtt_server, clientId, user, passwd);
}


void MqttClient::begin_impl(const char *mqtt_server, const char* clientId, const char *user, const char *passwd)
{
    m_mqtt_server = mqtt_server;
    free((void*)m_clientId);
    m_clientId = strdup(clientId); // strdup allocates memory for the new string. It is required if the clientId is generated dynamically (e.g., based on hostname) and may go out of scope after this function returns.
    m_user = user;
    m_passwd = passwd;
    Serial.printf("MQTT: server: '%s' port: %d clientId: '%s'\n", m_mqtt_server, m_mqtt_server_port, m_clientId);
    m_pubSubClient.setServer(mqtt_server, m_mqtt_server_port);
    m_pubSubClient.setCallback([this](char* topic, byte * payload, unsigned int length) {
        callback(topic, payload, length);
    });
}


bool MqttClient::connected() {
    return (m_enabled && m_pubSubClient.connected());
}


const char*  MqttClient::getClientId() {
    return this->m_clientId;
}


const char*  MqttClient::getUser() {
    return this->m_user;
}


const char*  MqttClient::getBroker() {
    return this->m_mqtt_server;
}


/*
-4 (CONNECTION_TIMEOUT): The most likely one for you. It means the 
   Keep-Alive timer expired because the PINGREQ wasn't sent or the 
   PINGRESP wasn't received (likely due to the 55ms "blind spots").
-2 (CONNECT_FAILED): The network connection was broken at the TCP level. 
   This happens if the Wi-Fi stack "choked" during a DHT read and couldn't 
   acknowledge a packet from Mosquitto.
-3 (MQTT_CONNECTION_LOST): The broker closed the connection. Usually happens 
   if you publish a NaN or malformed string, or if the broker didn't get 
   a heartbeat in time, or if another client with the same clientId connected to the broker.
-1 (DISCONNECTED): The client is disconnected cleanly, or failed to connect in the first place. This is the default state before connecting, and also the state after a clean disconnect.
 0 (CONNECTED): The client is connected to the broker.
#define MQTT_CONNECTION_TIMEOUT     -4
#define MQTT_CONNECTION_LOST        -3
#define MQTT_CONNECT_FAILED         -2
#define MQTT_DISCONNECTED           -1
#define MQTT_CONNECTED               0
#define MQTT_CONNECT_BAD_PROTOCOL    1
#define MQTT_CONNECT_BAD_CLIENT_ID   2
#define MQTT_CONNECT_UNAVAILABLE     3
#define MQTT_CONNECT_BAD_CREDENTIALS 4
#define MQTT_CONNECT_UNAUTHORIZED    5
*/
int MqttClient::getState() 
{
    if (m_enabled) {
        return this->m_pubSubClient.state();
    }
    return -100; // Not enabled
}


void MqttClient::handleClient() 
{
    if (!m_enabled) {
        return;
    }

    int currentState = m_pubSubClient.state();
    if (currentState != m_lastState) {
        Serial.print("MQTT State Changed: ");
        Serial.println(currentState);
        m_lastState = currentState;
    }

    if (!m_pubSubClient.connected()) {
        unsigned long now = millis();
        if (WiFi.status() != WL_CONNECTED) {
            // No attempt is possible without WiFi, so don't let it consume the reconnect interval.
            // Arm the timer instead, so MQTT reconnects on the first loop() pass after WiFi is back
            // (otherwise up to m_reconnectInterval later, losing a reading published in that window).
            m_lastReconnectAttempt = now - m_reconnectInterval - 1;
            return;
        }
        if (now - m_lastReconnectAttempt > m_reconnectInterval) {
            // Attempt to reconnect. The interval counts from the END of the attempt: a blocking
            // attempt longer than m_reconnectInterval would otherwise retry back-to-back and starve loop().
            m_lastReconnectAttempt = reconnect() ? 0 : millis();
        }
    } else {
        // Client connected
        m_pubSubClient.loop(); // Listen for MQTT events from broker. Called every single cycle for maximum stability
    }
}


bool MqttClient::reconnect() 
{
    if (WiFi.status() != WL_CONNECTED) {
        LOG("MQTT reconnect bypassed: WiFi not connected. Status:", (uint8_t) WiFi.status());
        return false;
    }
    LOG("MQTT reconnecting...");
    unsigned long start = millis();
    bool con;
    if (m_lastWillTopic != nullptr) {
        con = m_pubSubClient.connect(m_clientId, m_user, m_passwd, m_lastWillTopic, WILLQOS, m_lastWillRetain, m_lastWillMessage);
    }
    else {
        con = m_pubSubClient.connect(m_clientId, m_user, m_passwd);
    }
    if (con) {
        Serial.printf("#### MQTT CONNECTED clientId: %s Connection time: %lu ms\n", m_clientId, millis() - start);
        // Once connected, publish an announcement & resubscribe
        this->m_wifiClient.setNoDelay(true);
        publish_announcement();
        yield();
        clear_retained();   // Must precede resubscribe(): a retained command would be delivered on subscribe (see clear_retained())
        yield();
        resubscribe();
        yield();
        fire_connect_event(true);
        yield();
    }
    else {
        Serial.printf("#### MQTT CONNECT TIMEOUT %lu ms clientId: %s \n", millis() - start, m_clientId);
    }
    bool ret = m_pubSubClient.connected();
    Serial.printf("#### MQTT reconnection time used: %lu ms\n", millis() - start);
    return ret;
}


bool MqttClient::subscribe(const char* topic) 
{
    if (!m_enabled) 
        return false;
    return m_pubSubClient.subscribe(topic);
}


bool MqttClient::publish(const char* topic, const char* payload) {
    return publish(topic, payload, false);
}


bool MqttClient::publish(const char* topic, const char* payload, bool retained) 
{
    if (!m_enabled) {
        return false;
    }
    unsigned long start = millis();
    if (m_pubSubClient.connected()) {
        bool ret = m_pubSubClient.publish(topic, payload, retained);
        unsigned long time_used = millis() - start;
        Serial.printf("Publish: <%s> :[%s] %lu ms %s %s\n", topic, payload, time_used, retained ? "(RETAINED)" : "", ret ? "sent" : "- NOT SENT !!!");
        if (!ret) {
            if (MQTT_MAX_PACKET_SIZE < MQTT_MAX_HEADER_SIZE + 2 + strlen(topic) + strlen(payload)) {
                Serial.printf("WARN: topic: %d payload: %d; Total: %d > Max Len: %d\n",
                    strlen(topic),  strlen(payload), MQTT_MAX_HEADER_SIZE+2 + strlen(topic) +  strlen(payload), MQTT_MAX_PACKET_SIZE);
            }
        }
        // Process incoming MQTT events immediately after publish for maximum stability.
        // Reentrancy guard: loop() may dispatch an incoming message whose callback
        // publishes again (e.g. info/get -> send_info() -> publish()). Without the
        // guard, each nested publish would call loop() again, and a burst of queued
        // requests could recurse arbitrarily deep into the stack.
        if (!m_inLoop) {
            m_inLoop = true;
            m_pubSubClient.loop();
            m_inLoop = false;
        }
        return ret;
    }
    else {
        unsigned long time_used = millis() - start;
        Serial.print("Publish: NOTCONNECTED ");
        Serial.print(time_used);
        Serial.println(" ms");
        return false;
    }
}


void MqttClient::publish_announcement() 
{
    if (m_announcementTopic != nullptr) {
        const char *announcement = STR_CONNECTED;
        publish(m_announcementTopic, announcement);
        Serial.printf("MQTT announcement sent <%s>: [%s]\n", m_announcementTopic, announcement);
    }
}


// Deletes retained messages in the command topics set by setClearRetainedTopics().
//
// Why: commands must be sent with RETAIN=false. A command retained by mistake is stored by the broker
// and delivered again on every subscribe, i.e. after each reconnect and reboot (e.g. "valve/open" would
// reopen the valve after an emergency close). PubSubClient does not pass the retain flag to the callback,
// so the application cannot tell a stale retained message from a fresh command.
//
// How (MQTT 3.1.1, 3.3.1.3; the same in MQTT 5):
//   - The broker keeps one retained message per topic. A PUBLISH with RETAIN=1 replaces it,
//     a PUBLISH with RETAIN=0 leaves it alone. There is no topic to delete: only this stored message.
//   - A PUBLISH with RETAIN=1 and a zero-length payload removes the stored message, and the broker
//     does not store the empty one. A later subscriber gets nothing.
//
// Order matters: call this before resubscribe().
//   - The empty message is also delivered, as a normal zero-length message, to clients already
//     subscribed to the topic. If this client were subscribed, it would receive it as a command
//     (the payload of "valve/open" is ignored, so the valve would open).
//   - The broker handles the packets of one connection in order, so the stored message is gone
//     before the SUBSCRIBE that follows is processed.
//
// Side effect: other clients subscribed to these topics (MQTT Explorer, a Home Assistant automation)
// receive the empty message too on each connect of this device.
void MqttClient::clear_retained()
{
    for (int i = 0; i < m_clearRetainedTopics_cnt; i++) {
        publish(m_clearRetainedTopics[i], "", true); // RETAIN=1 + empty payload: the broker deletes the stored message
    }
}


void MqttClient::resubscribe()
{
    if (m_topics_cnt > 0) {
        Serial.println("MQTT subscribing");
        for(int i=0; i<m_topics_cnt; i++) {
            Serial.print("MQTT subscribing for ");
            Serial.println(m_topics[i]);
            subscribe(m_topics[i]);
        }
        Serial.println("MQTT subscribed");
    }
}


void MqttClient::callback(char* topic, byte* payload, unsigned int length) 
{
    Serial.print("Message arrived <");
    Serial.print(topic);
    Serial.print(">:[");
    for (unsigned int i = 0; i < length; i++) {
        Serial.print((char)payload[i]);
    }
    Serial.println("]");
    if (custom_callback) {
        custom_callback(topic, payload, length);
    }
}


void MqttClient::fire_connect_event(bool connected) 
{
    if (connect_event != nullptr) {
        connect_event(connected);
    }
}


void MqttClient::debug_info() {
    Serial.println("\nMqttClient.cpp:\n===============");
    Serial.printf("MQTT Broker: %s\n", m_mqtt_server!=nullptr ? m_mqtt_server : "NULL");
    Serial.printf("MQTT clientId: %s\n", m_clientId!=nullptr ? m_clientId : "NULL");
    Serial.printf("MQTT User: %s\n", m_user!=nullptr ? m_user : "NULL");
    // Serial.printf("MQTT Password: %s\n", m_passwd!=nullptr ? m_passwd : "NULL");
    Serial.printf("MQTT Client: %sCONNECTED\n", m_pubSubClient.connected() ? "" : "NOT ");
    Serial.printf("WiFi Client: %sCONNECTED\n", m_wifiClient.connected() ? "" : "NOT ");
    Serial.printf("WiFi Client local IP: %s port: %d\n", m_wifiClient.localIP().toString().c_str(), m_wifiClient.localPort());
    Serial.printf("WiFi Client remote IP: %s port: %d\n", m_wifiClient.remoteIP().toString().c_str(), m_wifiClient.remotePort());
    Serial.printf("WiFi Client TCP_NODELAY: %s\n", m_wifiClient.getNoDelay() ? "true" : "false");
}
