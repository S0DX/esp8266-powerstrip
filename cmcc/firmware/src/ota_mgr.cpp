#include "ota_mgr.h"
#include "config.h"
#include <ESP8266mDNS.h>

void (*OTAManager::progress_callback_)(unsigned int, unsigned int) = nullptr;
void (*OTAManager::end_callback_)() = nullptr;
void (*OTAManager::error_callback_)(short) = nullptr;

void OTAManager::init(const char* hostname, const char* password) {
    ArduinoOTA.setHostname(hostname);
    ArduinoOTA.setPassword(password);
    ArduinoOTA.setPort(OTA_PORT);

    ArduinoOTA.onStart([]() {
        Serial.println("[OTA] Update start");
    });

    ArduinoOTA.onEnd([]() {
        Serial.println("\n[OTA] Update complete, restarting...");
    });

    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        static uint8_t last = 0;
        uint8_t pct = progress / (total / 100);
        if (pct != last) {
            Serial.printf("[OTA] Progress: %u%%\r", pct);
            last = pct;
        }
        if (progress_callback_) {
            progress_callback_(progress, total);
        }
    });

    ArduinoOTA.onError([](ota_error_t error) {
        Serial.printf("\n[OTA] Error[%u]: ", error);
        switch (error) {
            case OTA_AUTH_ERROR:
                Serial.println("Auth Failed");
                break;
            case OTA_BEGIN_ERROR:
                Serial.println("Begin Failed");
                break;
            case OTA_CONNECT_ERROR:
                Serial.println("Connect Failed");
                break;
            case OTA_RECEIVE_ERROR:
                Serial.println("Receive Failed");
                break;
            case OTA_END_ERROR:
                Serial.println("End Failed");
                break;
            default:
                Serial.println("Unknown Error");
                break;
        }
        if (error_callback_) {
            error_callback_((short)error);
        }
    });

    if (WiFi.status() == WL_CONNECTED) {
        if (!MDNS.begin(hostname)) {
            Serial.println("[OTA] Error setting up MDNS responder!");
        } else {
            Serial.println("[OTA] mDNS responder started");
        }
    }

    ArduinoOTA.begin();
    String ipStr = (WiFi.status() == WL_CONNECTED) ?
        WiFi.localIP().toString() : WiFi.softAPIP().toString();
    Serial.printf("[OTA] OTA service started on %s:%d\n", ipStr.c_str(), OTA_PORT);
}

void OTAManager::handle() {
    ArduinoOTA.handle();
}

void OTAManager::setProgressCallback(void (*callback)(unsigned int, unsigned int)) {
    progress_callback_ = callback;
}

void OTAManager::setEndCallback(void (*callback)()) {
    end_callback_ = callback;
}

void OTAManager::setErrorCallback(void (*callback)(short)) {
    error_callback_ = callback;
}
