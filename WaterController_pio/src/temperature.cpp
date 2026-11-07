#include <OneWire.h>
#include <DallasTemperature.h>
#include "lib/MqttClient.h"
#include "main.h"

// DS18B20 probes, one per GPIO: a fault in one cable does not take down the other,
// and the role follows the connector, so a replaced probe needs no address setup.
#define TEMP_CONVERSION_MS  800  // 750 ms at 12 bits (factory default) plus a margin
#define TEMP_MAX_FAILURES   3    // Failed readings in a row before the status turns "error"
#define TEMP_POWER_ON_C     85.0f // Power-on value of the register: the conversion did not run

struct TempSensor
{
    const char *name;
    uint8_t pin;
    const char *topic;
    const char *statusTopic;
    OneWire wire;
    DallasTemperature dallas;
    DeviceAddress address;
    bool found;
    int failures;            // Failed readings in a row
    const char *status;
    float last;              // Valid while status is "ok"
};

static TempSensor sensors[] = {
    {"pipe", TEMP_PIPE_GPIO, TOPIC_TEMPERATURE_PIPE, TOPIC_TEMPERATURE_PIPE_STATUS},
    {"air",  TEMP_AIR_GPIO,  TOPIC_TEMPERATURE_AIR,  TOPIC_TEMPERATURE_AIR_STATUS},
};

static unsigned long temperature_lastActivity = 0L;
static unsigned long temperature_interval;  // Set by temperature_init()
static bool temperature_converting = false; // Conversion started, the result is read after TEMP_CONVERSION_MS

static void temperature_status_set(TempSensor &s, const char *status)
{
    bool changed = strcmp(status, s.status) != 0;
    s.status = status;
    if (changed) {
        mqttClient.publish(s.statusTopic, s.status, true);
    }
}

// Retained, so consumers can tell a sensor fault from a quiet sensor
void temperature_status_publish()
{
    for (TempSensor &s : sensors) {
        mqttClient.publish(s.statusTopic, s.status, true);
    }
}

// Searches the bus: a few tens of ms, so only at start and after the probe was lost
static void temperature_discover(TempSensor &s)
{
    s.dallas.begin();
    s.found = s.dallas.getAddress(s.address, 0);
    s.failures = 0;
    if (s.found) {
        Serial.printf("Temperature %s: DS18B20 found, %d bit\n", s.name, s.dallas.getResolution(s.address));
    }
    else {
        Serial.printf("Temperature %s: no DS18B20 on GPIO%u\n", s.name, s.pin);
        temperature_status_set(s, "no_sensor");
    }
}

void temperature_init(unsigned long interval)
{
    temperature_interval = interval;
    for (TempSensor &s : sensors) {
        s.status = "unknown"; // Until the first reading
        s.wire.begin(s.pin);
        s.dallas.setOneWire(&s.wire);
        s.dallas.setWaitForConversion(false); // requestTemperatures() must not block for 750 ms
        temperature_discover(s);
    }
    temperature_lastActivity = millis() - interval; // First reading right away
    Serial.printf("temperature_init\n");
}

// Publishes the last reading of each probe, or its status if there is none
void temperature_get()
{
    for (TempSensor &s : sensors) {
        if (strcmp(s.status, "ok") != 0) {
            mqttClient.publish(s.statusTopic, s.status, true);
            continue;
        }
        char buf[16];
        snprintf(buf, sizeof(buf), "%.2f", s.last);
        mqttClient.publish(s.topic, buf);
    }
}

static void temperature_read(TempSensor &s)
{
    float t = s.dallas.getTempC(s.address); // DEVICE_DISCONNECTED_C on a missing probe or a CRC error
    if (t == DEVICE_DISCONNECTED_C || t == TEMP_POWER_ON_C) {
        s.failures++;
        if (s.failures < TEMP_MAX_FAILURES) {
            Serial.printf("Temperature %s: read failed (%.2f), sample skipped (%d/%d)\n", s.name, t, s.failures, TEMP_MAX_FAILURES);
            return;
        }
        Serial.printf("Temperature %s: read failed (%.2f), marked as error\n", s.name, t);
        s.found = false; // Search the bus again on the next cycle
        temperature_status_set(s, "error");
        return;
    }
    s.failures = 0;
    s.last = t;
    Serial.printf("Temperature %s: %.2f C\n", s.name, t);
    temperature_status_set(s, "ok");

    char buf[16];
    snprintf(buf, sizeof(buf), "%.2f", t);
    mqttClient.publish(s.topic, buf);
}

void temperature_handle()
{
    unsigned long now = millis();

    if (temperature_converting) {
        if (now - temperature_lastActivity < TEMP_CONVERSION_MS) {
            return;
        }
        temperature_converting = false; // Before publishing: publish() runs a nested loop()
        for (TempSensor &s : sensors) {
            if (s.found) {
                temperature_read(s);
            }
        }
        return;
    }

    if (now - temperature_lastActivity >= temperature_interval) {
        temperature_lastActivity = now;
        for (TempSensor &s : sensors) {
            if (!s.found) {
                temperature_discover(s);
            }
            if (s.found) {
                s.dallas.requestTemperatures(); // One probe per bus: Skip ROM starts it
            }
        }
        temperature_converting = true;
    }
}

void temperature_debug_info()
{
    Serial.println("\ntemperature.cpp:\n================");
    for (TempSensor &s : sensors) {
        Serial.printf("%s (GPIO%u): status %s, found %d, failures %d, last %.2f C\n",
                      s.name, s.pin, s.status, s.found, s.failures, s.last);
    }
}
