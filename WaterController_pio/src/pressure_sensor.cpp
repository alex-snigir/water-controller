#include "lib\MqttClient.h"
#include "main.h"

// SWP-300: 0-10 bar, signal 0-5V
#define PRESSURE_SENSOR_BAR_PER_VOLT  2.0f
#define PRESSURE_SENSOR_MAX_BAR       10.0f
// Valid signal range, with a tolerance for ADC noise and sensor offset
#define PRESSURE_SENSOR_MIN_VOLTS     -0.1f
#define PRESSURE_SENSOR_MAX_VOLTS     5.1f

unsigned long read_pressure_lastActivity = 0L;
unsigned long read_pressure_interval = 15000;

static const char *pressure_status = "unknown"; // Until the first reading
static float pressure_last;                     // Valid while pressure_status is "ok" or "over_range"

// Retained, so consumers can tell a sensor fault from a quiet sensor
void pressure_status_publish() {
    mqttClient.publish(TOPIC_PRESSURE_STATUS, pressure_status, true);
}

static void pressure_status_set(const char *status) {
    bool changed = strcmp(status, pressure_status) != 0;
    pressure_status = status;
    if (changed) {
        pressure_status_publish();
    }
}

void pressure_sensor_init(unsigned long interval) {
    read_pressure_interval = interval;
    read_pressure_lastActivity = millis(); // The first interval starts here, not at boot
    Serial.printf("pressure_sensor_init\n");
}

// Publishes the last reading, or the status if there is none. Must not read the ADC:
// it runs in the MQTT callback, and a burst of requests would stall loop().
void pressure_get() {
    if (strcmp(pressure_status, "ok") != 0 && strcmp(pressure_status, "over_range") != 0) {
        pressure_status_publish();
        return;
    }
    char buf[32];
    snprintf(buf, sizeof(buf), "%.2f", pressure_last);
    mqttClient.publish(TOPIC_PRESSURE, buf);
}

static void pressure_read() {
    float volts;
    if (!ads_voltage_chnnel_get(ADS_CHANNEL_PRESSURE_SENSOR, &volts)) {
        Serial.println("Pressure: ADS1115 not available, skip publishing");
        pressure_status_set("no_adc");
        return;
    }
    if (volts < PRESSURE_SENSOR_MIN_VOLTS || volts > PRESSURE_SENSOR_MAX_VOLTS) {
        Serial.printf("Pressure: sensor voltage %f V out of range [%.1f..%.1f V], skip publishing\n",
                      volts, PRESSURE_SENSOR_MIN_VOLTS, PRESSURE_SENSOR_MAX_VOLTS);
        pressure_status_set("out_of_range");
        return;
    }
    float pressure = volts * PRESSURE_SENSOR_BAR_PER_VOLT;
    pressure_last = constrain(pressure, 0.0f, PRESSURE_SENSOR_MAX_BAR);
    Serial.printf("Pressure: %f bar [Volts: %f]\n", pressure_last, volts);
    // At the top of its range the sensor saturates: the real pressure is at least the published one
    pressure_status_set(pressure > PRESSURE_SENSOR_MAX_BAR ? "over_range" : "ok");
    pressure_get();
}

void pressure_sensor_handle() {
  unsigned long t = millis();
  if (t - read_pressure_lastActivity >= read_pressure_interval) {
    read_pressure_lastActivity = t;
    pressure_read();
  }
}
