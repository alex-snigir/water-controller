#include "lib\MqttClient.h"
#include "main.h"

// H-Bridge control for valve (2 relays or DRV8871 motor driver)

int valve_move_time_mills = 7000;
unsigned long valveMovingTimer = 0; // Таймер для отключения реле
bool isValveMoving = false;         // Флаг движения

#define VALVE_BUTTON_DEBOUNCE_MS 30 // Button level must be stable this long
#define VALVE_OUTPUT_MASK_MS     20 // C10/C11 hold IN1/IN2 high for up to 9 ms after an output goes low

unsigned long valveOutputOffTimer = 0; // When the outputs were last switched off

// There is no position feedback: the state follows the last command or button hold.
// It stays unknown after boot and after any partial travel, until a full move completes.
enum ValveState { VALVE_UNKNOWN, VALVE_OPENING, VALVE_OPEN, VALVE_CLOSING, VALVE_CLOSED };
static ValveState valveState = VALVE_UNKNOWN;
static bool valveStatePublishPending = false;

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

unsigned long manualMoveTimer = 0;            // When the current button move started
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

void valve_init()
{
    digitalWrite(VALVE_OPEN_PIN, LOW);
    digitalWrite(VALVE_CLOSE_PIN, LOW);
    pinMode(VALVE_OPEN_PIN, OUTPUT);
    pinMode(VALVE_CLOSE_PIN, OUTPUT);
    pinMode(VALVE_OPEN_STATUS, INPUT);  // No pull-ups: R4/R3 hold the level
    pinMode(VALVE_CLOSE_STATUS, INPUT);
}

// Функция полной остановки (снятие питания с катушек relay configuration)
void _stop_valve()
{
    digitalWrite(VALVE_OPEN_PIN, LOW);
    digitalWrite(VALVE_CLOSE_PIN, LOW);
    isValveMoving = false;
    valveOutputOffTimer = millis();
}

static void _button_pressed(ValveButton &button, ValveButton &other, unsigned long now)
{
    Serial.printf("Valve button pressed: %s\n", button.name);

    // The button wins: against a firmware output it would only brake the valve
    if (isValveMoving)
        _stop_valve();

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

static void _button_released(ValveButton &button, ValveButton &other, unsigned long now)
{
    Serial.printf("Valve button released: %s\n", button.name);

    if (other.pressed)
    {
        // The other button is still held and now moves the valve from an unknown position
        manualMoveTimer = now;
        manualStartState = VALVE_UNKNOWN;
        valveState = other.moving;
    }
    else if (manualStartState == button.reached || now - manualMoveTimer >= (unsigned long)valve_move_time_mills)
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

static void _button_handle(ValveButton &button, ValveButton &other, unsigned long now)
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
        _button_pressed(button, other, now);
    else
        _button_released(button, other, now);
}

void valve_handle()
{
    unsigned long now = millis();
    _button_handle(openButton, closeButton, now);
    _button_handle(closeButton, openButton, now);

    if (valveStatePublishPending)
    {
        valveStatePublishPending = false;
        valve_state_publish();
    }

    // Автоматическое отключение реле после завершения хода
    if (isValveMoving && (millis() - valveMovingTimer >= valve_move_time_mills))
    {
        _stop_valve();
        valveState = (valveState == VALVE_OPENING) ? VALVE_OPEN : VALVE_CLOSED;
        Serial.println("Valve is stopped.");
        valve_state_publish();
    }
}

// OTA blocks loop(), so valve_handle() could not stop the valve on time.
// No publish here: it may dispatch a queued command, which would restart the valve.
// After a successful update the state is sent on MQTT connect, after a failed one from valve_handle().
void valve_ota_start()
{
    if (isValveMoving)
    {
        _stop_valve();
        valveState = VALVE_UNKNOWN;
        valveStatePublishPending = true;
        Serial.println("Valve output is off: OTA started.");
    }
}

static void _valve_open()
{
    LOG("valve_open: CALLED");
    if (isValveMoving)
        _stop_valve();

    digitalWrite(VALVE_CLOSE_PIN, LOW); // Убеждаемся, что второе реле выключено
    digitalWrite(VALVE_OPEN_PIN, HIGH); // Включаем первое реле (+12V на Красный)

    valveMovingTimer = millis();
    isValveMoving = true;
    valveState = VALVE_OPENING;
    Serial.println("Valve is opening...");
    valve_state_publish();
}

static void _valve_close()
{
    LOG("valve_close: CALLED");
    if (isValveMoving)
        _stop_valve();

    digitalWrite(VALVE_OPEN_PIN, LOW);   // Убеждаемся, что первое реле выключено
    digitalWrite(VALVE_CLOSE_PIN, HIGH); // Включаем второе реле (+12V на Синий)

    valveMovingTimer = millis();
    isValveMoving = true;
    valveState = VALVE_CLOSING;
    Serial.println("Valve is closing...");
    valve_state_publish();
}

// A held button overrides commands: the opposite command would only brake the valve.
// The state is republished so the sender sees the command was not taken.
static bool _command_blocked()
{
    if (!openButton.pressed && !closeButton.pressed)
        return false;
    Serial.println("Valve command ignored: button is pressed");
    valve_state_publish();
    return true;
}

void valve_open()
{
    if (!_command_blocked())
        _valve_open();
}

void valve_close()
{
    if (!_command_blocked())
        _valve_close();
}