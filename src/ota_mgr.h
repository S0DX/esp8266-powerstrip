#ifndef OTA_MGR_H
#define OTA_MGR_H

#include <Arduino.h>
#include <ArduinoOTA.h>

class OTAManager {
public:
    static void init(const char* hostname, const char* password);
    static void handle();

private:
};

#endif