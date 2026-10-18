#include "lib\MqttClient.h"
#include "main.h"

unsigned long read_pressure_lastActivity = 0L;
unsigned long read_pressure_interval = 15000;

void pressure_sensor_init(unsigned long interval) {
    read_pressure_interval = interval;
    Serial.printf("pressure_sensor_init\n");
}

void pressure_get() {
    float volts;
    if (!ads_voltage_chnnel_get(ADS_CHANNEL_PRESSURE_SENSOR, &volts)) {
        Serial.println("Presure: ADS1115 not available, skip publishing");
        return;
    }
    float presure = volts*2;
    Serial.printf("Presure: %f bar [Volts: %f]\n", presure, volts);
    char buf[32];
    snprintf(buf, sizeof(buf), "%f", presure);
    mqttClient.publish(TOPIC_PRESURE, buf);
}

void pressure_sensor_handle() {
  unsigned long t = millis();
  if (t - read_pressure_lastActivity >= read_pressure_interval) {
    read_pressure_lastActivity = t;
    pressure_get();
  }
}