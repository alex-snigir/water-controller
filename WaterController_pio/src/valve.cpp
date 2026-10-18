#include "main.h"

// H-Bridge control for valve (2 relays or DRV8871 motor driver)

int valve_move_time_mills = 7000;
unsigned long valveMovingTimer = 0; // Таймер для отключения реле
bool isValveMoving = false;         // Флаг движения

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
        Serial.println("Valve is stopped.");
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
    Serial.println("Valve is opening...");
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
    Serial.println("Valve is closing...");
}

void valve_open()
{
    _valve_open();
}

void valve_close()
{
    _valve_close();
}