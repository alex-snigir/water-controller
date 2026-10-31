#pragma once

#include <Arduino.h>

class OTAClient {
    
    public:
        OTAClient() = default;
        void begin(const char *host, const char *passwd);
        void setStartCallback(void (*callback)()); // Called when an update starts; loop() is blocked until it ends
    
        String getHostname();
        void debug_info(void);

        void handleClient();
};

extern OTAClient otaClient;
