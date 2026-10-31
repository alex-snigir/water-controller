#include "lib\MqttClient.h"
#include "main.h"

// H-Bridge control for valve (2 relays or DRV8871 motor driver)

int valve_move_time_mills = 7000;
unsigned long valveMovingTimer = 0; // Таймер для отключения реле
bool isValveMoving = false;         // Флаг движения

// There is no position feedback: the state follows the last firmware command.
// It stays unknown after boot until the first command completes.
enum ValveState { VALVE_UNKNOWN, VALVE_OPENING, VALVE_OPEN, VALVE_CLOSING, VALVE_CLOSED };
static ValveState valveState = VALVE_UNKNOWN;

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
}

// Функция полной остановки (снятие питания с катушек relay configuration)
void _stop_valve()
{
    digitalWrite(VALVE_OPEN_PIN, LOW);
    digitalWrite(VALVE_CLOSE_PIN, LOW);
    isValveMoving = false;
}

void valve_handle()
{
    // Автоматическое отключение реле после завершения хода
    if (isValveMoving && (millis() - valveMovingTimer >= valve_move_time_mills))
    {
        _stop_valve();
        valveState = (valveState == VALVE_OPENING) ? VALVE_OPEN : VALVE_CLOSED;
        Serial.println("Valve is stopped.");
        valve_state_publish();
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

void valve_open()
{
    _valve_open();
}

void valve_close()
{
    _valve_close();
}