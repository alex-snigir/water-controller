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
#endif
#if defined(ESP32)
// ESP32: D1 mini:
#define TEST_WIFI_LED          32
#define VALVE_LED              32
#define FLOW_LED               25
#define ERROR_LED              26
#define ADC_SDA_GPIO           21
#define ADC_SCL_GPIO           22
#define WATER_FLOW_GPIO        17
#define VALVE_OPEN_PIN         19  //
#define VALVE_CLOSE_PIN        18  //
#define AC_POWER_DETECT_GPIO   ?
#endif

#endif
