#include "lib/MqttClient.h"
#include "main.h"

// Warnings from the temperature and pressure readings. Home Assistant sends the notifications;
// nothing here acts on the valve. Each alarm has a hysteresis, so it does not chatter at the threshold.
#define FREEZE_ON_C             3.0f  // Pipe below: risk of freezing
#define FREEZE_OFF_C            4.0f
#define OVERHEAT_ON_C           55.0f // Controller box above: electronics overheat
#define OVERHEAT_OFF_C          50.0f
#define CONDENSATION_ON_C       2.0f  // Air temperature minus box dew point below: risk of condensation
#define CONDENSATION_OFF_C      3.0f

// The pressure sensor is downstream of the valve (about 4 bar after the reducer). With the valve closed
// the house pressure may fall to 0 legitimately; with it open, 0 means a broken sensor wire,
// no sensor power, no mains water or a burst pipe. A broken wire reads 0 V, the same as 0 bar.
#define LOW_PRESSURE_ON_BAR     0.5f
#define LOW_PRESSURE_OFF_BAR    1.0f
#define LOW_PRESSURE_SETTLE_MS  30000L // After the valve reports open, before the pressure is judged
#define LOW_PRESSURE_HOLD_MS    30000L // Low this long (3 readings at 10 s) before the alarm

#define ERROR_LED_ON            LOW   // LED cathode is on the pin
#define ERROR_LED_OFF           HIGH
#define ERROR_LED_FAULT_MS      1000  // Half period of the slow blink on a sensor fault

enum AlarmState { ALARM_UNKNOWN, ALARM_OFF, ALARM_ON };

struct Alarm
{
    const char *name;
    const char *topic;
    AlarmState state;
    bool active;        // Last known on/off: kept while unknown, so the hysteresis survives a sensor fault
};

static Alarm freezeAlarm       = {"freeze",       TOPIC_ALARM_FREEZE,       ALARM_UNKNOWN, false};
static Alarm overheatAlarm     = {"overheat",     TOPIC_ALARM_OVERHEAT,     ALARM_UNKNOWN, false};
static Alarm condensationAlarm = {"condensation", TOPIC_ALARM_CONDENSATION, ALARM_UNKNOWN, false};
static Alarm lowPressureAlarm  = {"low_pressure", TOPIC_ALARM_LOW_PRESSURE, ALARM_UNKNOWN, false};
static Alarm *alarms[] = {&freezeAlarm, &overheatAlarm, &condensationAlarm, &lowPressureAlarm};

static bool valveWasOpen = false;
static unsigned long valveOpenSince = 0L;
static bool pressureWasLow = false;
static unsigned long pressureLowSince = 0L;

static const char *alarm_state_name(AlarmState state)
{
    switch (state) {
    case ALARM_ON:  return "on";
    case ALARM_OFF: return "off";
    default:        return "unknown";
    }
}

// Retained: "unknown" while a sensor it needs has no valid reading, so a missing sensor does not read as "off"
static void alarm_publish(const Alarm &a)
{
    mqttClient.publish(a.topic, alarm_state_name(a.state), true);
}

void alarms_publish()
{
    for (Alarm *a : alarms) {
        alarm_publish(*a);
    }
}

static void alarm_set(Alarm &a, AlarmState state)
{
    if (state == a.state) {
        return;
    }
    a.state = state;
    Serial.printf("Alarm %s: %s\n", a.name, alarm_state_name(state));
    alarm_publish(a);
}

// An active alarm clears only past its off threshold, an inactive one starts at the on threshold.
// After "unknown" the alarm goes on from its last known state.
static void alarm_eval(Alarm &a, bool valid, bool onCondition, bool offCondition)
{
    if (!valid) {
        alarm_set(a, ALARM_UNKNOWN);
        return;
    }
    a.active = a.active ? !offCondition : onCondition;
    alarm_set(a, a.active ? ALARM_ON : ALARM_OFF);
}

static void low_pressure_eval(unsigned long now)
{
    float bar = 0;
    bool valid = pressure_value(&bar);
    bool open = valve_is_open();

    if (open && !valveWasOpen) {
        valveOpenSince = now;
    }
    valveWasOpen = open;

    bool low = valid && bar < LOW_PRESSURE_ON_BAR;
    if (low && !pressureWasLow) {
        pressureLowSince = now;
    }
    pressureWasLow = low;

    if (valid && !open) {
        lowPressureAlarm.active = false; // Closed or unknown valve: low pressure is not a fault
        alarm_set(lowPressureAlarm, ALARM_OFF);
        return;
    }
    bool onCondition = open && now - valveOpenSince >= LOW_PRESSURE_SETTLE_MS &&
                       low && now - pressureLowSince >= LOW_PRESSURE_HOLD_MS;
    alarm_eval(lowPressureAlarm, valid, onCondition, bar > LOW_PRESSURE_OFF_BAR);
}

void alarms_init()
{
    digitalWrite(ERROR_LED, ERROR_LED_OFF);
    pinMode(ERROR_LED, OUTPUT);
}

// LED4: on while any alarm is active, slow blink on a sensor fault, off otherwise
static void error_led_handle(unsigned long now)
{
    bool on = false;
    for (Alarm *a : alarms) {
        on = on || a->state == ALARM_ON;
    }
    if (!on && (temperature_faulty() || box_faulty())) {
        on = (now / ERROR_LED_FAULT_MS) & 1;
    }
    digitalWrite(ERROR_LED, on ? ERROR_LED_ON : ERROR_LED_OFF);
}

// Cheap enough for every loop(): the readings it uses change every 10 s at most
void alarms_handle()
{
    unsigned long now = millis();
    float pipe = 0, air = 0, box = 0, dewpoint = 0; // Only used when the matching *Valid is true
    bool pipeValid = temperature_value(TEMP_PROBE_PIPE, &pipe);
    bool airValid = temperature_value(TEMP_PROBE_AIR, &air);
    bool boxValid = box_value(&box, &dewpoint);

    alarm_eval(freezeAlarm, pipeValid, pipe < FREEZE_ON_C, pipe > FREEZE_OFF_C);
    alarm_eval(overheatAlarm, boxValid, box > OVERHEAT_ON_C, box < OVERHEAT_OFF_C);
    alarm_eval(condensationAlarm, airValid && boxValid,
               air - dewpoint < CONDENSATION_ON_C, air - dewpoint > CONDENSATION_OFF_C);
    low_pressure_eval(now);

    error_led_handle(now);
}

void alarms_debug_info()
{
    Serial.println("\nalarms.cpp:\n===========");
    for (Alarm *a : alarms) {
        Serial.printf("%s: %s\n", a->name, alarm_state_name(a->state));
    }
    Serial.printf("Sensor fault: %d\n", temperature_faulty() || box_faulty());
}
