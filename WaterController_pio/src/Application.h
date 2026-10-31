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
#define ADC_SDA_GPIO           21
#define ADC_SCL_GPIO           22
#define WATER_FLOW_GPIO        23
#define VALVE_OPEN_PIN         19  // MC output: open command (IN1 via D3)
#define VALVE_CLOSE_PIN        18  // MC output: close command (IN2 via D4)
#define VALVE_OPEN_STATUS      34  // input: IN1 level (Open button or MC), via R7. Not valve position
#define VALVE_CLOSE_STATUS     35  // input: IN2 level (Close button or MC), via R8. Not valve position
#define AC_POWER_DETECT_GPIO   ?
#endif

#endif
