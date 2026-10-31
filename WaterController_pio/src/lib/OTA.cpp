// OTA
//
// Versions:
//   v0.1 20220115
//   v0.2 20220712 - Enhancements
//   v0.3 20221014 - Added debug_info(), log fixes
//   v0.4 20221219 - Added MDNS to debug_info()
//   v0.5 20230526 - Added MdnsClient.h
//   v0.6 20260606 - Refactoring, AI suggested improvements, etc...
//   v0.7 20260607 - Minor fixes
//   v0.8 20260610 - Minor fix
//   v0.9 20260618 - Guard against total == 0 before dividing
//   v0.10 20261031 - Added setStartCallback()

#include <ArduinoOTA.h>
#include "OTA.h"

#if defined(ESP8266) && !defined(NO_GLOBAL_INSTANCES) && !defined(NO_GLOBAL_MDNS)
#include <ESP8266mDNS.h>
#endif
#if defined(ESP32) && !defined(NO_GLOBAL_INSTANCES) && !defined(NO_GLOBAL_MDNS)
#include <ESPmDNS.h>
#endif

#include "MdnsClient.h"
#include "Log.h"

OTAClient otaClient; // Created OTAClient instance

static void (*start_callback)() = nullptr;

void OTAClient::setStartCallback(void (*callback)())
{
    start_callback = callback;
}

void OTAClient::begin(const char *host, const char *passwd)
{
    ArduinoOTA.setHostname(host);
    ArduinoOTA.setPassword(passwd);

    ArduinoOTA.onStart([]() {
        String type;
        if (ArduinoOTA.getCommand() == U_FLASH) {
            type = "sketch";
        } else { // U_FS
            type = "filesystem";
        }

        // NOTE: if updating FS this would be the place to unmount FS using FS.end()
        LOG("Start OTA updating ", type.c_str());

        if (start_callback) {
            start_callback();
        }
    });

    ArduinoOTA.onEnd([]() { 
        LOG("End OTA"); 
    });

    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        if (total == 0) {
            return;
        }
        char buf[20];
        sprintf(buf, "Progress: %u%%", (progress * 100) / total);
        LOG(buf);
    });

    ArduinoOTA.onError([](ota_error_t error) {
        switch (error) {
            case OTA_AUTH_ERROR:    LOG_ERROR("OTA Auth Failed");    break;
            case OTA_BEGIN_ERROR:   LOG_ERROR("OTA Begin Failed");   break;
            case OTA_CONNECT_ERROR: LOG_ERROR("OTA Connect Failed"); break;
            case OTA_RECEIVE_ERROR: LOG_ERROR("OTA Receive Failed"); break;
            case OTA_END_ERROR:     LOG_ERROR("OTA End Failed");     break;
            default:                LOG_ERROR("OTA Unknown Error");  break;
        }
    });

    ArduinoOTA.begin();

    LOG("OTA ready");
    #if defined(ESP8266) && !defined(NO_GLOBAL_INSTANCES) && !defined(NO_GLOBAL_MDNS)
    LOG("OTA: mDNS: ", getHostname() + ".local");
    #endif
    #if defined(ESP32)
    LOG("OTA: mDNS: ", getHostname() + ".local");
    #endif
}


void OTAClient::handleClient()
{
    ArduinoOTA.handle();
}


String OTAClient::getHostname()
{
    return ArduinoOTA.getHostname();
}


void OTAClient::debug_info(void)
{
    Serial.println("\nOTAClient.cpp:\n==============");
    Serial.printf("OTA mDNS: %s.local\n", ArduinoOTA.getHostname().c_str());
    // Serial.printf("MDNS isRunning: %s\n", MDNS.isRunning() ? "true" : "false");
    mdnsClient.debug_info();
}
