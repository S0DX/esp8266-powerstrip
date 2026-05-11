#ifndef OTA_MGR_H
#define OTA_MGR_H

#include <Arduino.h>
#include <ArduinoOTA.h>

class OTAManager {
public:
    static void init(const char* hostname, const char* password);
    static void handle();
    static void setProgressCallback(void (*callback)(unsigned int, unsigned int));
    static void setEndCallback(void (*callback)());
    static void setErrorCallback(void (*callback)(short));

private:
    static void (*progress_callback_)(unsigned int, unsigned int);
    static void (*end_callback_)();
    static void (*error_callback_)(short);
};

#endif
