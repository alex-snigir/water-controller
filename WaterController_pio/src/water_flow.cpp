#include "lib\MqttClient.h"
#include "main.h"

// YF-B10 476 pulses/L
#define PULSES_PER_LITER 476

unsigned long water_flow_calculation_lastActivity = 0L;
unsigned long water_flow_calculation_interval = 60000;

volatile unsigned int pulses;   // Updated from ISR
float water_rate;
static uint64_t total_pulses = 0; // Since boot. Published as is, so a failed publish loses nothing

static portMUX_TYPE pulses_mux = portMUX_INITIALIZER_UNLOCKED;

void IRAM_ATTR water_flow_pulse();

void water_flow_sensor_init() {
    Serial.printf("water_flow_sensor_init\n");
    pinMode(WATER_FLOW_GPIO, INPUT_PULLUP); // Keep the input from floating when the sensor is disconnected
    attachInterrupt(digitalPinToInterrupt(WATER_FLOW_GPIO), water_flow_pulse, FALLING);
}

// Interrupt Service Routine (ISR)
// This function is called automatically when the interrupt condition is met.
void IRAM_ATTR water_flow_pulse() {
    portENTER_CRITICAL_ISR(&pulses_mux);
    pulses++;
    portEXIT_CRITICAL_ISR(&pulses_mux);
}

void water_flow_handle() {
  unsigned long t = millis();
  if (t - water_flow_calculation_lastActivity >= water_flow_calculation_interval) {
    water_flow_calculation_lastActivity = t;
    portENTER_CRITICAL(&pulses_mux);
    unsigned int pulses_read = pulses;
    pulses = 0;
    portEXIT_CRITICAL(&pulses_mux);
    water_rate = pulses_read / (float)PULSES_PER_LITER; // Liters per calculation interval (1 min)
    Serial.printf("Water Flow pulses: %u Rate: %f L/min\n", pulses_read, water_rate);
    water_flow_get();

    total_pulses += pulses_read;
    char buf[32];
    snprintf(buf, sizeof(buf), "%.3f", total_pulses / (double)PULSES_PER_LITER);
    mqttClient.publish(TOPIC_FLOW_TOTAL, buf);
  }
}

// Publishes the last calculated rate: 0 until the first interval ends.
// Must not read the pulse counter: that would cut the current interval short.
void water_flow_get() {
    char buf[32];
    snprintf(buf, sizeof(buf), "%f", water_rate);
    mqttClient.publish(TOPIC_FLOW_RATE, buf);
}
