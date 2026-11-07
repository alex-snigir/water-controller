#ifndef SN_APPLICATION_H
#define SN_APPLICATION_H

#if ARDUINO >= 100
//#pragma message "ALEX: ARDUINO >= 100"
#include <Arduino.h>       // for delayMicroseconds, digitalPinToBitMask, etc
#else
//#pragma message "ALEX: ARDUINO < 100"
#include "WProgram.h"      // for delayMicroseconds
#include "pins_arduino.h"  // for digitalPinToBitMask, etc
#endif

#include "lib\Log.h"


#if defined(ESP8266)
// Not supported
#error ESP8266 Not supported
#endif
#if defined(ESP32)
// ESP32: ESP-32U-38PINs:
#define TEST_WIFI_LED          32
#define VALVE_LED              33
#define FLOW_LED               25
#define ERROR_LED              26
#define I2C_SDA_GPIO           21
#define I2C_SCL_GPIO           22
#define WATER_FLOW_GPIO        23
#define VALVE_OPEN_PIN         19   // MC output: open command (IN1 via D3)
#define VALVE_CLOSE_PIN        18   // MC output: close command (IN2 via D4)
#define VALVE_OPEN_STATUS      34   // input: IN1 level (Open button or MC), via R7. Not valve position
#define VALVE_CLOSE_STATUS     35   // input: IN2 level (Close button or MC), via R8. Not valve position
#define TEMP_PIPE_GPIO          5   // DS18B20 via J4, pull-up R15
#define TEMP_AIR_GPIO          15   // DS18B20 via J5, pull-up R16
#define AC_POWER_DETECT_GPIO   ?
#endif

// Box temperature/humidity sensor on J6 (I2C)
#define BOX_SENSOR_SHT41       1
#define BOX_SENSOR_SHTC3       2    // For tests: no heater
#ifndef BOX_SENSOR
#define BOX_SENSOR             BOX_SENSOR_SHT41
#endif
#define SHT41_I2C_ADDRESS      0x44 // SHT41-AD1B; 0x45 for SHT41-BD1B
#define SHTC3_I2C_ADDRESS      0x70

#endif
