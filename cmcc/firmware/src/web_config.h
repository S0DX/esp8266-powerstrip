#ifndef WEB_CONFIG_H
#define WEB_CONFIG_H

#include <ESP8266WebServer.h>
#include <memory>

class WebConfigServer {
public:
    static void init();
    static void handle();
    static void setSaveCallback(void (*callback)(const char* ssid, const char* pass));

private:
    static std::unique_ptr<ESP8266WebServer> server_;
    static void (*save_callback_)(const char*, const char*);
};

#endif
