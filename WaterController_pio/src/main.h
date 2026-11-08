#ifndef SN_MAIN_H
#define SN_MAIN_H

#include "Application.h"

extern const char APP_NAME[];
extern const char APP_VER[];
extern volatile time_t boottime;
extern volatile int timeset_cnt;
extern uint32_t sketch_size;
time_t boottime_get(time_t now);

#define TOPIC_KEEPALIVE           _AREA_ "/keepalive/" _HOST_NAME_
#define TOPIC_ALIVE               _AREA_ "/alive"
#define TOPIC_ALIVE_GET           _AREA_ "/alive/get"
#define TOPIC_INFO                _AREA_ "/" _HOST_NAME_ "/info"
#define TOPIC_INFO_GET            _AREA_ "/" _HOST_NAME_ "/info/get"
#define TOPIC_CONNECTION_INFO     _AREA_ "/" _HOST_NAME_ "/connection/info"

#define TOPIC_VALVE_ENABLED       _AREA_ "/" _HOST_NAME_ "/valve_enable"
#define TOPIC_VALVE_ENABLED_GET   _AREA_ "/" _HOST_NAME_ "/valve_enable/get"

// #define TOPIC_VALVE            _AREA_ "/" _HOST_NAME_ "/valve"
#define TOPIC_VALVE_OPEN          _AREA_ "/" _HOST_NAME_ "/valve/open"
#define TOPIC_VALVE_CLOSE         _AREA_ "/" _HOST_NAME_ "/valve/close"
#define TOPIC_VALVE_STATE         _AREA_ "/" _HOST_NAME_ "/valve/state"
#define TOPIC_VALVE_STATE_GET     _AREA_ "/" _HOST_NAME_ "/valve/state/get"
#define TOPIC_VALVE_MANUAL        _AREA_ "/" _HOST_NAME_ "/valve/manual"

#define TOPIC_PRESSURE              _AREA_ "/" _HOST_NAME_ "/pressure"
#define TOPIC_PRESSURE_GET          _AREA_ "/" _HOST_NAME_ "/pressure/get"
#define TOPIC_PRESSURE_STATUS       _AREA_ "/" _HOST_NAME_ "/pressure/status"

#define TOPIC_FLOW_RATE            _AREA_ "/" _HOST_NAME_ "/flowrate"
#define TOPIC_FLOW_RATE_GET        _AREA_ "/" _HOST_NAME_ "/flowrate/get"
#define TOPIC_FLOW_TOTAL           _AREA_ "/" _HOST_NAME_ "/flowtotal"

#define TOPIC_TEMPERATURE_GET          _AREA_ "/" _HOST_NAME_ "/temperature/get"
#define TOPIC_TEMPERATURE_PIPE         _AREA_ "/" _HOST_NAME_ "/temperature/pipe"
#define TOPIC_TEMPERATURE_PIPE_STATUS  _AREA_ "/" _HOST_NAME_ "/temperature/pipe/status"
#define TOPIC_TEMPERATURE_AIR          _AREA_ "/" _HOST_NAME_ "/temperature/air"
#define TOPIC_TEMPERATURE_AIR_STATUS   _AREA_ "/" _HOST_NAME_ "/temperature/air/status"

#define TOPIC_BOX_GET              _AREA_ "/" _HOST_NAME_ "/box/get"
#define TOPIC_BOX_TEMPERATURE      _AREA_ "/" _HOST_NAME_ "/box/temperature"
#define TOPIC_BOX_HUMIDITY         _AREA_ "/" _HOST_NAME_ "/box/humidity"
#define TOPIC_BOX_DEWPOINT         _AREA_ "/" _HOST_NAME_ "/box/dewpoint"
#define TOPIC_BOX_STATUS           _AREA_ "/" _HOST_NAME_ "/box/status"

#define TOPIC_ALARM_GET            _AREA_ "/" _HOST_NAME_ "/alarm/get"
#define TOPIC_ALARM_FREEZE         _AREA_ "/" _HOST_NAME_ "/alarm/freeze"
#define TOPIC_ALARM_OVERHEAT       _AREA_ "/" _HOST_NAME_ "/alarm/overheat"
#define TOPIC_ALARM_CONDENSATION   _AREA_ "/" _HOST_NAME_ "/alarm/condensation"
#define TOPIC_ALARM_LOW_PRESSURE   _AREA_ "/" _HOST_NAME_ "/alarm/low_pressure"

#define KEEP_ALIVE_MESSAGE_INTERVAL 60000L // 1 minute

// Callbacks:
void mqtt_callback(char* topic, byte* payload, unsigned int length);
void test_change_event_callback(uint8_t event);
void keepAlive_event_callback();


void debug_init();

void valve_init();
void valve_handle();
void valve_open();
void valve_close();
void valve_state_publish();
void valve_ota_start();
void valve_restart_prepare();
void valve_debug_info();
bool valve_is_open();

#define ADS_CHANNEL_PRESSURE_SENSOR 0

void ads_init();
void ads_handle();
void ads_debug_info();
enum AdsResult { ADS_OK, ADS_SKIPPED, ADS_UNAVAILABLE };
AdsResult ads_voltage_chnnel_get(int channel, float *volts);

void pressure_sensor_init(unsigned long interval);
void pressure_sensor_handle();
void pressure_get();
void pressure_status_publish();
void pressure_debug_info();
bool pressure_value(float *bar);

void water_flow_sensor_init(unsigned long interval);
void water_flow_handle();
void water_flow_get();
void water_flow_debug_info();

void temperature_init(unsigned long interval);
void temperature_handle();
void temperature_get();
void temperature_status_publish();
void temperature_debug_info();
enum TempProbe { TEMP_PROBE_PIPE, TEMP_PROBE_AIR }; // Index in the sensors[] of temperature.cpp
bool temperature_value(TempProbe probe, float *celsius);
bool temperature_faulty();

void box_sensor_init(unsigned long interval);
void box_sensor_handle();
void box_get();
void box_status_publish();
void box_debug_info();
bool box_value(float *celsius, float *dewpoint);
bool box_faulty();

void alarms_init();
void alarms_handle();
void alarms_publish();
void alarms_debug_info();

#endif