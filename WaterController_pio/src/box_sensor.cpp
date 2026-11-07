#include <Wire.h>
#include "lib/MqttClient.h"
#include "main.h"

// Box air: SHT41 (or SHTC3 for tests, see BOX_SENSOR in Application.h) on the ADS1115 I2C bus.
// Driven over Wire directly: the Adafruit libraries wait for the measurement with delay(),
// and the SHT41 heater takes 1.1 s.

#if BOX_SENSOR == BOX_SENSOR_SHT41
#define BOX_I2C_ADDRESS       SHT41_I2C_ADDRESS
#define BOX_MEASURE_MS        10    // High precision: 8.3 ms max
#define BOX_HEATER_RH         90.0f // Heat after a reading at or above this humidity
#define BOX_HEATER_INTERVAL   1800000L // 30 min: the heater duty cycle must stay low
#define BOX_HEATER_MS         1200  // 110 mW for 1 s, then a measurement: 1.1 s max
#define SHT41_CMD_MEASURE     0xFD  // High precision
#define SHT41_CMD_HEATER      0x2F  // 110 mW, 1 s
#elif BOX_SENSOR == BOX_SENSOR_SHTC3
#define BOX_I2C_ADDRESS       SHTC3_I2C_ADDRESS
#define BOX_MEASURE_MS        15    // Normal mode: 12.1 ms max
#define SHTC3_CMD_WAKEUP      0x3517
#define SHTC3_CMD_MEASURE     0x7866 // Normal mode, temperature first, no clock stretching
#define SHTC3_CMD_SLEEP       0xB098
#else
#error BOX_SENSOR must be BOX_SENSOR_SHT41 or BOX_SENSOR_SHTC3
#endif

#define BOX_MAX_FAILURES      3     // Failed readings in a row before the status changes

// Magnus formula coefficients for the dew point over water
#define DEWPOINT_A            17.62f
#define DEWPOINT_B            243.12f

enum BoxPhase { BOX_IDLE, BOX_MEASURING, BOX_HEATING };
static BoxPhase box_phase = BOX_IDLE;
static unsigned long box_phase_start = 0L;
static unsigned long box_lastActivity = 0L;
static unsigned long box_interval;          // Set by box_sensor_init()
#if BOX_SENSOR == BOX_SENSOR_SHT41
static unsigned long box_heater_last = 0L;  // Set by box_sensor_init() so the heater may run at once
#endif

static int box_failures = 0;                // Failed readings in a row
static bool box_failures_no_answer;         // All of them were an unanswered address
static const char *box_status = "unknown";  // Until the first reading
static float box_temperature;               // Valid while box_status is "ok"
static float box_humidity;
static float box_dewpoint;

// Retained, so consumers can tell a sensor fault from a quiet sensor
void box_status_publish()
{
    mqttClient.publish(TOPIC_BOX_STATUS, box_status, true);
}

static void box_status_set(const char *status)
{
    bool changed = strcmp(status, box_status) != 0;
    box_status = status;
    if (changed) {
        box_status_publish();
    }
}

// CRC-8, polynomial 0x31, init 0xFF: the same for SHT4x and SHTC3
static uint8_t box_crc(const uint8_t *data)
{
    uint8_t crc = 0xFF;
    for (int i = 0; i < 2; i++) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; bit++) {
            crc = (crc & 0x80) ? (crc << 1) ^ 0x31 : (crc << 1);
        }
    }
    return crc;
}

// Returns the Wire.endTransmission() code: 0 - ok, 2 - no device at the address
static uint8_t box_command(uint16_t cmd)
{
    Wire.beginTransmission(BOX_I2C_ADDRESS);
    if (cmd > 0xFF) {
        Wire.write((uint8_t)(cmd >> 8));
    }
    Wire.write((uint8_t)(cmd & 0xFF));
    return Wire.endTransmission();
}

static uint8_t box_start_measure()
{
#if BOX_SENSOR == BOX_SENSOR_SHT41
    return box_command(SHT41_CMD_MEASURE);
#else
    uint8_t err = box_command(SHTC3_CMD_WAKEUP);
    if (err != 0) {
        return err;
    }
    delayMicroseconds(240); // Wake-up time
    err = box_command(SHTC3_CMD_MEASURE);
    if (err != 0) {
        box_command(SHTC3_CMD_SLEEP); // Otherwise it stays awake until the next good cycle
    }
    return err;
#endif
}

// Reads the raw temperature and humidity words, both CRC-checked
static bool box_read_raw(uint16_t *rawT, uint16_t *rawRH)
{
    uint8_t buf[6];
    bool ok = Wire.requestFrom((uint8_t)BOX_I2C_ADDRESS, (uint8_t)6) == 6;
    if (ok) {
        for (int i = 0; i < 6; i++) {
            buf[i] = Wire.read();
        }
        ok = box_crc(&buf[0]) == buf[2] && box_crc(&buf[3]) == buf[5];
    }
#if BOX_SENSOR == BOX_SENSOR_SHTC3
    box_command(SHTC3_CMD_SLEEP);
#endif
    if (!ok) {
        return false;
    }
    *rawT = (buf[0] << 8) | buf[1];
    *rawRH = (buf[3] << 8) | buf[4];
    return true;
}

// A NACK also comes from a present sensor (busy, or a transfer spoiled by motor noise):
// "no_sensor" only if the address went unanswered every time in a row.
static void box_failed(bool noAnswer, const char *reason)
{
    if (box_failures == 0) {
        box_failures_no_answer = true;
    }
    box_failures_no_answer = box_failures_no_answer && noAnswer;
    box_failures++;
    if (box_failures < BOX_MAX_FAILURES) {
        Serial.printf("Box sensor: %s, sample skipped (%d/%d)\n", reason, box_failures, BOX_MAX_FAILURES);
        return;
    }
    const char *status = box_failures_no_answer ? "no_sensor" : "error";
    Serial.printf("Box sensor: %s, marked as %s\n", reason, status);
    box_failures = 0;
    box_status_set(status);
}

static float box_dewpoint_calc(float t, float rh)
{
    float gamma = logf(max(rh, 0.1f) / 100.0f) + DEWPOINT_A * t / (DEWPOINT_B + t);
    return DEWPOINT_B * gamma / (DEWPOINT_A - gamma);
}

// Publishes the last reading, or the status if there is none
void box_get()
{
    if (strcmp(box_status, "ok") != 0) {
        box_status_publish();
        return;
    }
    char buf[16];
    snprintf(buf, sizeof(buf), "%.2f", box_temperature);
    mqttClient.publish(TOPIC_BOX_TEMPERATURE, buf);
    snprintf(buf, sizeof(buf), "%.1f", box_humidity);
    mqttClient.publish(TOPIC_BOX_HUMIDITY, buf);
    snprintf(buf, sizeof(buf), "%.2f", box_dewpoint);
    mqttClient.publish(TOPIC_BOX_DEWPOINT, buf);
}

static void box_read()
{
    uint16_t rawT, rawRH;
    if (!box_read_raw(&rawT, &rawRH)) {
        box_failed(false, "read failed");
        return;
    }
#if BOX_SENSOR == BOX_SENSOR_SHT41
    float t = -45.0f + 175.0f * rawT / 65535.0f;
    float rh = -6.0f + 125.0f * rawRH / 65535.0f;
#else
    float t = -45.0f + 175.0f * rawT / 65536.0f;
    float rh = 100.0f * rawRH / 65536.0f;
#endif
    box_failures = 0;
    box_temperature = t;
    box_humidity = constrain(rh, 0.0f, 100.0f);
    box_dewpoint = box_dewpoint_calc(box_temperature, box_humidity);
    Serial.printf("Box sensor: %.2f C, %.1f %%RH, dew point %.2f C\n", box_temperature, box_humidity, box_dewpoint);
    box_status_set("ok");
    box_get();
}

#if BOX_SENSOR == BOX_SENSOR_SHT41
// Dries the sensor after long high humidity. The heater also warms the air around it,
// so the next reading, a whole interval later, is the first one taken.
static bool box_heater_due(unsigned long now)
{
    return strcmp(box_status, "ok") == 0 && box_humidity >= BOX_HEATER_RH &&
           now - box_heater_last >= BOX_HEATER_INTERVAL;
}
#endif

void box_sensor_init(unsigned long interval)
{
    box_interval = interval;
    box_lastActivity = millis() - interval; // First reading right away; Wire is started by ads_init()
#if BOX_SENSOR == BOX_SENSOR_SHT41
    box_heater_last = millis() - BOX_HEATER_INTERVAL;
#endif
    Serial.printf("box_sensor_init: %s at 0x%02X\n", BOX_SENSOR == BOX_SENSOR_SHT41 ? "SHT41" : "SHTC3", BOX_I2C_ADDRESS);
}

void box_sensor_handle()
{
    unsigned long now = millis();

    switch (box_phase) {
    case BOX_MEASURING:
        if (now - box_phase_start < BOX_MEASURE_MS) {
            return;
        }
        box_phase = BOX_IDLE; // Before publishing: publish() runs a nested loop()
        box_read();
#if BOX_SENSOR == BOX_SENSOR_SHT41
        if (box_phase == BOX_IDLE && box_heater_due(now)) {
            if (box_command(SHT41_CMD_HEATER) == 0) {
                Serial.println("Box sensor: heater on");
                box_heater_last = now;
                box_phase = BOX_HEATING;
                box_phase_start = now;
            }
        }
#endif
        return;

#if BOX_SENSOR == BOX_SENSOR_SHT41
    case BOX_HEATING: {
        if (now - box_phase_start < BOX_HEATER_MS) {
            return;
        }
        // The heater ends with a measurement of the heated sensor: read it out and drop it
        uint16_t rawT, rawRH;
        box_read_raw(&rawT, &rawRH);
        box_phase = BOX_IDLE;
        Serial.println("Box sensor: heater off");
        return;
    }
#endif

    default:
        break;
    }

    if (now - box_lastActivity >= box_interval) {
        box_lastActivity = now;
        uint8_t err = box_start_measure();
        if (err != 0) {
            box_failed(err == 2, err == 2 ? "no answer" : "command failed");
            return;
        }
        box_phase = BOX_MEASURING;
        box_phase_start = now;
    }
}

void box_debug_info()
{
    Serial.println("\nbox_sensor.cpp:\n===============");
    Serial.printf("Sensor: %s at 0x%02X\n", BOX_SENSOR == BOX_SENSOR_SHT41 ? "SHT41" : "SHTC3", BOX_I2C_ADDRESS);
    Serial.printf("Status: %s, failures %d of %d\n", box_status, box_failures, BOX_MAX_FAILURES);
    if (strcmp(box_status, "ok") == 0) {
        Serial.printf("Last: %.2f C, %.1f %%RH, dew point %.2f C\n", box_temperature, box_humidity, box_dewpoint);
    }
#if BOX_SENSOR == BOX_SENSOR_SHT41
    unsigned long since = millis() - box_heater_last;
    if (since < BOX_HEATER_INTERVAL) {
        Serial.printf("Heater: used %lu s ago\n", since / 1000);
    }
    else {
        Serial.println("Heater: ready");
    }
#endif
}
