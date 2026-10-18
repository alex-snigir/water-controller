// MdnsClient
//
// v0.1 20230526 - ESP8266 & ESP32 supported
// v0.2 20230606 - Refactoring, AI suggested improvements, etc...
// v0.3 20230608 - Minor fixes
//
// TODO:
//   > Investigate how to verify MDNS status on ESP32 and ESP8266

#include "MdnsClient.h"

#if defined(ESP8266) && !defined(NO_GLOBAL_INSTANCES) && !defined(NO_GLOBAL_MDNS)
#include <ESP8266mDNS.h>
#endif
#if defined(ESP32) && !defined(NO_GLOBAL_INSTANCES) && !defined(NO_GLOBAL_MDNS)
#include <ESPmDNS.h>
#endif


#include "Log.h"

MdnsClient mdnsClient;  // Created MdnsClient instance


void MdnsClient::debug_info(void)
{
    Serial.println("\nMdnsClient.cpp:\n===============");
    #ifdef ESP8266
    Serial.printf("MDNS isRunning: %s\n", MDNS.isRunning() ? "true" : "false");
    #endif
    if (MDNS.hostname(0).length() > 0) {
        Serial.println("MDNS last query results:");
        for (int i = 0; MDNS.hostname(i).length() > 0; i++) {
            Serial.printf("  [%d] %s (%s:%d)\n", i,
                MDNS.hostname(i).c_str(),
                MDNS.IP(i).toString().c_str(),
                MDNS.port(i));
        }
    } else {
        Serial.println("MDNS last query results: none");
    }
}

void MdnsClient::findAllServices()
{
    /*
    stcMDNSService *pService = MDNS.m_pServices;
    while (pService)
    {
        printService(pService);
        pService = MDNS.m_pServices;
    }
    */
}

/*
void MdnsClient::printService(stcMDNSService *pService)
{
    Serial.print("Name: ");
    Serial.print(m_pcName;
    Serial.print(" AutoName: ");
    Serial.print(m_bAutoName);  // Name was set automatically to hostname (if no name was supplied)
    Serial.print(" Service: ");
    Serial.print(m_pcService);
    Serial.print(" Protocol: ");
    Serial.print(m_pcProtocol);
    Serial.print(" Port: ");
    Serial.print(m_u16Port);
    Serial.println();
}
*/

void MdnsClient::findService(const char *service, const char *proto)
{
    Serial.printf("Browsing for service _%s._%s.local. ... ", service, proto);
    int n = MDNS.queryService(service, proto);
    if (n == 0)
    {
        Serial.println("no services found");
    }
    else
    {
        Serial.print(n);
        Serial.println(" service(s) found");
        for (int i = 0; i < n; ++i)
        {
            // Print details for each service found
            Serial.print("  ");
            Serial.print(i + 1);
            Serial.print(": ");
            Serial.print(MDNS.hostname(i));
            Serial.print(" (");
            Serial.print(MDNS.IP(i));
            Serial.print(":");
            Serial.print(MDNS.port(i));
            Serial.println(")");
        }
    }
    Serial.println();
}
