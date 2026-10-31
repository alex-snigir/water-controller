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

void pressure_sensor_init(unsigned long interval) {
    read_pressure_interval = interval;
    Serial.printf("pressure_sensor_init\n");
}

void pressure_get() {
    float volts;
    if (!ads_voltage_chnnel_get(ADS_CHANNEL_PRESSURE_SENSOR, &volts)) {
        Serial.println("Pressure: ADS1115 not available, skip publishing");
        return;
    }
    if (volts < PRESSURE_SENSOR_MIN_VOLTS || volts > PRESSURE_SENSOR_MAX_VOLTS) {
        Serial.printf("Pressure: sensor voltage %f V out of range [%.1f..%.1f V], skip publishing\n",
                      volts, PRESSURE_SENSOR_MIN_VOLTS, PRESSURE_SENSOR_MAX_VOLTS);
        return;
    }
    float pressure = constrain(volts * PRESSURE_SENSOR_BAR_PER_VOLT, 0.0f, PRESSURE_SENSOR_MAX_BAR);
    Serial.printf("Pressure: %f bar [Volts: %f]\n", pressure, volts);
    char buf[32];
    snprintf(buf, sizeof(buf), "%f", pressure);
    mqttClient.publish(TOPIC_PRESSURE, buf);
}

void pressure_sensor_handle() {
  unsigned long t = millis();
  if (t - read_pressure_lastActivity >= read_pressure_interval) {
    read_pressure_lastActivity = t;
    pressure_get();
  }
}
