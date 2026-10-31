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

static unsigned long valveOutputOffTimer = 0; // When the outputs were last switched off

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
    bool pressed;       // Debounced
    bool level;         // Last raw level
    unsigned long levelTimer;
};
static ValveButton openButton  = {VALVE_OPEN_STATUS,  VALVE_OPENING, VALVE_OPEN,   "open",  false, false, 0};
static ValveButton closeButton = {VALVE_CLOSE_STATUS, VALVE_CLOSING, VALVE_CLOSED, "close", false, false, 0};

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

void valve_debug_info()
{
    Serial.println("\nvalve.cpp:\n==========");
    Serial.printf("State: %s\n", valve_state_name());
    Serial.printf("Moving: %d\n", isValveMoving);
    Serial.printf("Drive pending: %d\n", valveDrivePending);
    if (isValveMoving)
        Serial.printf("Moving for: %lu of %lu ms\n", millis() - valveMovingTimer, valve_move_time_mills);
    Serial.printf("Publish pending: %d\n", valveStatePublishPending);
    Serial.printf("Open button: pressed %d, IN1 %d\n", openButton.pressed, digitalRead(openButton.statusPin));
    Serial.printf("Close button: pressed %d, IN2 %d\n", closeButton.pressed, digitalRead(closeButton.statusPin));
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

static void button_pressed(ValveButton &button, ValveButton &other, unsigned long now)
{
    Serial.printf("Valve button pressed: %s\n", button.name);

    // The button wins: against a firmware output it would only brake the valve
    if (isValveMoving || valveDrivePending)
        valve_stop();

    if (other.pressed)
    {
        valveState = VALVE_UNKNOWN; // Both buttons: brake, the valve stands wherever it is
    }
    else
    {
        manualMoveTimer = now;
        manualStartState = valveState;
        valveState = button.moving;
    }
    valve_state_publish();
    mqttClient.publish(TOPIC_VALVE_MANUAL, button.name);
}

static void button_released(ValveButton &button, ValveButton &other, unsigned long now)
{
    Serial.printf("Valve button released: %s\n", button.name);

    if (other.pressed)
    {
        // The other button is still held and now moves the valve from an unknown position
        manualMoveTimer = now;
        manualStartState = VALVE_UNKNOWN;
        valveState = other.moving;
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

void valve_handle()
{
    unsigned long now = millis();
    valve_led_handle(now);
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
    else if (openButton.pressed || closeButton.pressed)
        Serial.println("Valve command ignored: button is pressed");
    else
        return false;
    valve_state_publish();
    return true;
}

static void valve_command(ValveState moving)
{
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