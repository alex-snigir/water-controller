#include <Preferences.h>
#include "lib/MqttClient.h"
#include "main.h"

// H-bridge control for valve: DRV8871, IN1/IN2. The DPDT+SPDT relay build (power + direction pins) is not supported

static const unsigned long valve_move_time_mills = 7000;
static unsigned long valveMovingTimer = 0; // Таймер хода крана
static bool isValveMoving = false;         // Флаг движения

#define VALVE_BUTTON_DEBOUNCE_MS 30 // Button level must be stable this long
#define VALVE_OUTPUT_MASK_MS     20 // C10/C11 hold IN1/IN2 high for up to 9 ms after an output goes low
#define VALVE_REVERSE_PAUSE_MS   20 // Let IN1/IN2 settle before driving the opposite output

#define VALVE_LED_ON             LOW  // LED cathode is on the pin
#define VALVE_LED_OFF            HIGH
#define VALVE_LED_MOVING_MS      250  // Half period of the blink while moving
#define VALVE_LED_UNKNOWN_PERIOD 2000 // Unknown state: a short flash, so it does not read as closed
#define VALVE_LED_UNKNOWN_FLASH  100

#define VALVE_LOCK_DEBOUNCE_MS   50
#define VALVE_NVS_NAMESPACE      "valve"
#define VALVE_NVS_KEY_ENABLE     "enable"

static unsigned long valveOutputOffTimer = 0; // When the outputs were last switched off

// Two independent bans on opening; closing always works.
// Lock Switch J7 cuts IN1 in hardware, so it stops the firmware and SW1 alike: IO13 = 1 (also with the cable pulled out).
// valve_enable bans firmware opens only: SW1 drives IN1 past the ESP32. Kept in NVS.
static bool valveLocked = false;        // Debounced IO13
static bool lockLevel = false;          // Last raw IO13 level
static unsigned long lockLevelTimer = 0;
static bool valveEnabled = true;        // Default for the first boot

// There is no position feedback: the state follows the last command or button hold.
// It stays unknown after boot and after any partial travel, until a full move completes.
enum ValveState { VALVE_UNKNOWN, VALVE_OPENING, VALVE_OPEN, VALVE_CLOSING, VALVE_CLOSED };
static ValveState valveState = VALVE_UNKNOWN;
static bool valveStatePublishPending = false;
static bool valveDrivePending = false; // valveState is set, the outputs wait for the reverse pause

// SW1/SW2 drive IN1/IN2 directly, past the ESP32. VALVE_*_STATUS only shows the level on them,
// which is also high while the firmware drives the same pin.
struct ValveButton
{
    uint8_t statusPin;
    ValveState moving;  // State while this button is held
    ValveState reached; // State after a full travel
    const char *name;
    bool lockable;      // J7 cuts this button off DRV8871 (IN1 only)
    bool pressed;       // Debounced
    bool active;        // Pressed and reaches DRV8871: false while Open is held under the lock
    bool level;         // Last raw level
    unsigned long levelTimer;
};
static ValveButton openButton  = {VALVE_OPEN_STATUS,  VALVE_OPENING, VALVE_OPEN,   "open",  true,  false, false, false, 0};
static ValveButton closeButton = {VALVE_CLOSE_STATUS, VALVE_CLOSING, VALVE_CLOSED, "close", false, false, false, false, 0};

static unsigned long manualMoveTimer = 0;            // When the current button move started
static ValveState manualStartState = VALVE_UNKNOWN; // State before the current button move

static const char *valve_state_name()
{
    switch (valveState)
    {
    case VALVE_OPENING: return "opening";
    case VALVE_OPEN:    return "open";
    case VALVE_CLOSING: return "closing";
    case VALVE_CLOSED:  return "closed";
    default:            return "unknown";
    }
}

// Retained, so a new subscriber sees the state without asking.
// Call last: publish() may dispatch a queued command that changes the state.
void valve_state_publish()
{
    mqttClient.publish(TOPIC_VALVE_STATE, valve_state_name(), true);
}

// Fully open as far as the firmware knows: there is no position feedback
bool valve_is_open()
{
    return valveState == VALVE_OPEN;
}

const char *valve_lock_name()
{
    return valveLocked ? "locked" : "unlocked";
}

// Retained, like valve/state
void valve_lock_publish()
{
    mqttClient.publish(TOPIC_VALVE_LOCK, valve_lock_name(), true);
}

bool valve_enabled()
{
    return valveEnabled;
}

// Retained, like valve/state
void valve_enable_publish()
{
    mqttClient.publish(TOPIC_VALVE_ENABLED, valveEnabled ? "1" : "0", true);
}

// Opening is banned now, by J7 or valve_enable: LED4 shows it
bool valve_open_blocked()
{
    return valveLocked || !valveEnabled;
}

// Not retained: an event, not a state
static void valve_rejected_publish(const char *reason)
{
    alarms_led_rejected();
    mqttClient.publish(TOPIC_VALVE_REJECTED, reason);
}

// Written to NVS only on a change, to spare the flash. Republished either way, as a reply to the sender.
void valve_enable_set(bool enable)
{
    if (enable != valveEnabled)
    {
        valveEnabled = enable;
        Serial.printf("Valve open %s\n", enable ? "enabled" : "disabled");
        Preferences prefs;
        if (prefs.begin(VALVE_NVS_NAMESPACE, false))
        {
            if (prefs.putUChar(VALVE_NVS_KEY_ENABLE, enable ? 1 : 0) == 0)
                Serial.println("ERROR: valve_enable: NVS write failed");
            prefs.end();
        }
        else
        {
            Serial.println("ERROR: valve_enable: NVS open failed");
        }
    }
    valve_enable_publish();
}

// valve_enable/set: "1" enables, "0" disables opening; anything else, empty included, is ignored
void valve_enable_command(const byte *payload, unsigned int length)
{
    if (length == 1 && (payload[0] == '0' || payload[0] == '1'))
    {
        valve_enable_set(payload[0] == '1');
        return;
    }
    Serial.printf("valve_enable/set ignored: payload \"%.*s\" (%u bytes), expected \"0\" or \"1\"\n",
                  (int)(length < 16 ? length : 16), (const char *)payload, length);
}

void valve_debug_info()
{
    Serial.println("\nvalve.cpp:\n==========");
    Serial.printf("State: %s\n", valve_state_name());
    Serial.printf("Lock: %s, IO13 %d\n", valve_lock_name(), digitalRead(VALVE_LOCK_GPIO));
    Serial.printf("valve_enable: %d\n", valveEnabled);
    Serial.printf("Moving: %d\n", isValveMoving);
    Serial.printf("Drive pending: %d\n", valveDrivePending);
    if (isValveMoving)
        Serial.printf("Moving for: %lu of %lu ms\n", millis() - valveMovingTimer, valve_move_time_mills);
    Serial.printf("Publish pending: %d\n", valveStatePublishPending);
    Serial.printf("Open button: pressed %d, active %d, IN1 %d\n", openButton.pressed, openButton.active, digitalRead(openButton.statusPin));
    Serial.printf("Close button: pressed %d, active %d, IN2 %d\n", closeButton.pressed, closeButton.active, digitalRead(closeButton.statusPin));
}

void valve_init()
{
    digitalWrite(VALVE_OPEN_PIN, LOW);
    digitalWrite(VALVE_CLOSE_PIN, LOW);
    pinMode(VALVE_OPEN_PIN, OUTPUT);
    pinMode(VALVE_CLOSE_PIN, OUTPUT);
    pinMode(VALVE_OPEN_STATUS, INPUT);  // No pull-ups: R4/R3 hold the level
    pinMode(VALVE_CLOSE_STATUS, INPUT);
    digitalWrite(VALVE_LED, VALVE_LED_OFF);
    pinMode(VALVE_LED, OUTPUT);

    // Known before the first command: R18/C14 settle in about 1 ms, long before setup() runs
    pinMode(VALVE_LOCK_GPIO, INPUT);    // No pull-up: R18 is external
    valveLocked = (digitalRead(VALVE_LOCK_GPIO) == HIGH);
    lockLevel = valveLocked;
    lockLevelTimer = millis();

    // Read-only: no namespace yet on the first boot, then the default stays
    Preferences prefs;
    if (prefs.begin(VALVE_NVS_NAMESPACE, true))
    {
        valveEnabled = (prefs.getUChar(VALVE_NVS_KEY_ENABLE, 1) != 0);
        prefs.end();
    }
}

// Функция полной остановки (IN1 = IN2 = 0: coast)
static void valve_stop()
{
    digitalWrite(VALVE_OPEN_PIN, LOW);
    digitalWrite(VALVE_CLOSE_PIN, LOW);
    isValveMoving = false;
    valveDrivePending = false;
    valveOutputOffTimer = millis();
}

// Drives the outputs for valveState
static void valve_drive()
{
    bool opening = (valveState == VALVE_OPENING);
    digitalWrite(opening ? VALVE_CLOSE_PIN : VALVE_OPEN_PIN, LOW);  // Убеждаемся, что второй выход выключен
    digitalWrite(opening ? VALVE_OPEN_PIN : VALVE_CLOSE_PIN, HIGH); // Включаем выход (Open: +12V на Красный, Close: +12V на Синий)

    valveMovingTimer = millis();
    isValveMoving = true;
}

// Starts the move now, or from valve_handle() if the outputs were switched off a moment ago
static void valve_start(ValveState moving)
{
    if (isValveMoving)
        valve_stop();

    valveState = moving;
    valveDrivePending = (millis() - valveOutputOffTimer < VALVE_REVERSE_PAUSE_MS);
    if (!valveDrivePending)
        valve_drive();
}

// A held button now reaches DRV8871: on a press, or when J7 is unlocked while Open is held
static void button_start(ValveButton &button, ValveButton &other, unsigned long now)
{
    button.active = true;

    // The button wins: against a firmware output it would only brake the valve
    if (isValveMoving || valveDrivePending)
        valve_stop();

    if (other.active)
    {
        valveState = VALVE_UNKNOWN; // Both buttons: brake, the valve stands wherever it is
    }
    else
    {
        manualMoveTimer = now;
        manualStartState = valveState;
        valveState = button.moving;
    }
}

// The other button is still held and now moves the valve from an unknown position
static void button_takeover(ValveButton &button, unsigned long now)
{
    manualMoveTimer = now;
    manualStartState = VALVE_UNKNOWN;
    valveState = button.moving;
}

static void button_pressed(ValveButton &button, ValveButton &other, unsigned long now)
{
    Serial.printf("Valve button pressed: %s\n", button.name);

    // J7 cuts IN1: IO34 sees the press, but the valve does not move and a firmware close goes on
    if (button.lockable && valveLocked)
    {
        Serial.println("Valve button rejected: lock switch");
        mqttClient.publish(TOPIC_VALVE_MANUAL, button.name);
        valve_rejected_publish("locked");
        return;
    }

    button_start(button, other, now);
    valve_state_publish();
    mqttClient.publish(TOPIC_VALVE_MANUAL, button.name);
}

static void button_released(ValveButton &button, ValveButton &other, unsigned long now)
{
    Serial.printf("Valve button released: %s\n", button.name);

    // A press rejected by the lock (or cut by it) did not move the valve
    if (!button.active)
    {
        mqttClient.publish(TOPIC_VALVE_MANUAL, "released");
        return;
    }
    button.active = false;

    if (other.active)
    {
        button_takeover(other, now);
    }
    else if (manualStartState == button.reached || now - manualMoveTimer >= valve_move_time_mills)
    {
        valveState = button.reached;
    }
    else
    {
        valveState = VALVE_UNKNOWN; // Stopped part-way
    }
    valve_state_publish();
    mqttClient.publish(TOPIC_VALVE_MANUAL, "released");
}

static void button_handle(ValveButton &button, ValveButton &other, unsigned long now)
{
    // The input cannot show a button while the firmware holds its own pin high, or just after
    bool masked = (isValveMoving && valveState == button.moving) || (now - valveOutputOffTimer < VALVE_OUTPUT_MASK_MS);
    bool level = masked ? button.pressed : (digitalRead(button.statusPin) == HIGH);

    if (level != button.level)
    {
        button.level = level;
        button.levelTimer = now;
    }
    if (level == button.pressed || now - button.levelTimer < VALVE_BUTTON_DEBOUNCE_MS)
        return;

    button.pressed = level;
    if (level)
        button_pressed(button, other, now);
    else
        button_released(button, other, now);
}

// Open: on. Closed: off. Moving: blinks. Unknown: short flashes.
/*
┌──────────────────┬───────────────────────────────────────┐
│   valve/state    │              VALVE_LED                │
├──────────────────┼───────────────────────────────────────┤
│ open             │ горит                                 │
├──────────────────┼───────────────────────────────────────┤
│ closed           │ не горит                              │
├──────────────────┼───────────────────────────────────────┤
│ opening, closing │ мигает: 250 мс горит, 250 мс не горит │
├──────────────────┼───────────────────────────────────────┤
│ unknown          │ короткая вспышка 100 мс раз в 2 с     │
└──────────────────┴───────────────────────────────────────┘

*/
static void valve_led_handle(unsigned long now)
{
    bool on;
    switch (valveState)
    {
    case VALVE_OPEN:    on = true; break;
    case VALVE_CLOSED:  on = false; break;
    case VALVE_UNKNOWN: on = (now % VALVE_LED_UNKNOWN_PERIOD < VALVE_LED_UNKNOWN_FLASH); break;
    default:            on = ((now / VALVE_LED_MOVING_MS) & 1); break;
    }
    digitalWrite(VALVE_LED, on ? VALVE_LED_ON : VALVE_LED_OFF);
}

static bool valve_abort(const char *reason);

// J7: IO13 = 1 locked. Only opening is affected: IN2 goes past J7, so a close goes on.
static void lock_handle(unsigned long now)
{
    bool level = (digitalRead(VALVE_LOCK_GPIO) == HIGH);
    if (level != lockLevel)
    {
        lockLevel = level;
        lockLevelTimer = now;
    }
    if (level == valveLocked || now - lockLevelTimer < VALVE_LOCK_DEBOUNCE_MS)
        return;

    valveLocked = level;
    Serial.printf("Lock switch: %s\n", valve_lock_name());

    if (valveLocked)
    {
        // IN1 is cut, the motor has already stopped part-way
        if (valveState == VALVE_OPENING)
            valve_abort("lock switch");
        if (openButton.active)
        {
            openButton.active = false;
            if (closeButton.active)
                button_takeover(closeButton, now); // Was a brake, now Close alone drives
            else
                valveState = VALVE_UNKNOWN;
            valveStatePublishPending = true;
        }
    }
    else if (openButton.pressed && !openButton.active)
    {
        // IN1 is connected again under the held button: the valve starts opening
        button_start(openButton, closeButton, now);
        valveStatePublishPending = true;
    }
    valve_lock_publish();
}

void valve_handle()
{
    unsigned long now = millis();
    valve_led_handle(now);
    lock_handle(now);
    button_handle(openButton, closeButton, now);
    button_handle(closeButton, openButton, now);

    if (valveDrivePending && (now - valveOutputOffTimer >= VALVE_REVERSE_PAUSE_MS))
    {
        valveDrivePending = false;
        valve_drive();
    }

    if (valveStatePublishPending)
    {
        valveStatePublishPending = false;
        valve_state_publish();
    }

    // Автоматическое отключение выхода после завершения хода
    if (isValveMoving && (millis() - valveMovingTimer >= valve_move_time_mills))
    {
        valve_stop();
        valveState = (valveState == VALVE_OPENING) ? VALVE_OPEN : VALVE_CLOSED;
        Serial.println("Valve is stopped.");
        valve_state_publish();
    }
}

// Cuts a firmware move short: the position is unknown after that. Returns false if the valve was idle.
static bool valve_abort(const char *reason)
{
    if (!isValveMoving && !valveDrivePending)
        return false;
    valve_stop();
    valveState = VALVE_UNKNOWN;
    valveStatePublishPending = true;
    Serial.printf("Valve output is off: %s.\n", reason);
    return true;
}

// OTA blocks loop(), so valve_handle() could not stop the valve on time.
// No publish here: it may dispatch a queued command, which would restart the valve.
// After a successful update the state is sent on MQTT connect, after a failed one from valve_handle().
void valve_ota_start()
{
    valve_abort("OTA started");
}

// Unlike OTA, a queued command does no harm here: the reset right after cuts it off.
// The delay lets the state leave before the reset drops the connection.
void valve_restart_prepare()
{
    if (valve_abort("restart"))
    {
        valve_state_publish();
        delay(100);
    }
}

// A repeated command must not restart the move: the 7 s limit would never expire.
// A held button overrides commands: the opposite command would only brake the valve.
// The state is republished so the sender gets a reply either way.
static bool command_blocked(ValveState moving)
{
    if ((isValveMoving || valveDrivePending) && valveState == moving)
        Serial.println("Valve command ignored: already moving this way");
    else if (openButton.active || closeButton.active) // Open held under the lock does not reach DRV8871
        Serial.println("Valve command ignored: button is pressed");
    else
        return false;
    valve_state_publish();
    return true;
}

// Opening needs IO13 = 0 and valve_enable = 1. The state is left as it is: the reply is valve/rejected.
static bool open_rejected()
{
    const char *reason = valveLocked ? "locked" : (!valveEnabled ? "disabled" : nullptr);
    if (reason == nullptr)
        return false;
    Serial.printf("Valve open rejected: %s\n", reason);
    valve_rejected_publish(reason);
    return true;
}

static void valve_command(ValveState moving)
{
    if (moving == VALVE_OPENING && open_rejected())
        return;
    if (command_blocked(moving))
        return;

    valve_start(moving);
    Serial.printf("Valve is %s...\n", valve_state_name());
    valve_state_publish();
}

void valve_open()
{
    valve_command(VALVE_OPENING);
}

void valve_close()
{
    valve_command(VALVE_CLOSING);
}