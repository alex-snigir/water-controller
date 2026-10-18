#pragma once

#include <Arduino.h>

class OTAClient {
    
    public:
        OTAClient() = default;
        void begin(const char *host, const char *passwd);
    
        String getHostname();
        void debug_info(void);

        void handleClient();
};

extern OTAClient otaClient;
