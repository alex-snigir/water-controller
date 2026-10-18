#pragma once

class MdnsClient {

  public:
    MdnsClient() = default;

    void findAllServices();
    // void printService(stcMDNSService *pService);
    void findService(const char * service, const char * proto);

    void debug_info();

};

extern MdnsClient mdnsClient;

