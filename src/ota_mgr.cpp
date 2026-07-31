#include "ota_mgr.h"
#include "config.h"
#include "wifi_mgr.h"
#include <ESP8266mDNS.h>

static bool ota_started = false;

void OTAManager::init(const char* hostname, const char* password) {
    ArduinoOTA.setPort(OTA_PORT);
    ArduinoOTA.setHostname(hostname);
    ArduinoOTA.setPassword(password);

    ArduinoOTA.onStart([]() {
        String type = (ArduinoOTA.getCommand() == U_FLASH) ? "sketch" : "filesystem";
        Serial.println("[OTA] Start updating " + type);
        Serial.println("[OTA] Stopping AP mode to fix routing...");
        WiFi.softAPdisconnect(true);
        delay(100);
        Serial.println("[OTA] AP mode stopped, STA only now");
    });

    ArduinoOTA.onEnd([]() {
        Serial.println("\n[OTA] Complete, rebooting...");
    });

    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        static uint8_t last = 0;
        uint8_t pct = (total > 0) ? (progress * 100 / total) : 0;
        if (pct != last) {
            Serial.printf("[OTA] Progress: %u%%\r", pct);
            last = pct;
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
                Serial.println("Connect Failed - AP mode may be interfering");
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
    });

    Serial.printf("[OTA] Configured (hostname: %s, port: %d)\n", hostname, OTA_PORT);
}

void OTAManager::handle() {
    if (!ota_started) {
        if (WiFi.status() == WL_CONNECTED) {
            // STA 已连接：断开 AP 避免双模路由问题，再启动 OTA
            if (WiFi.getMode() & WIFI_AP) {
                Serial.println("[OTA] Disconnecting AP mode for reliable OTA...");
                WiFi.softAPdisconnect(true);
                delay(100);
            }
            // OTA 使用 WiFiManager 已启动的 mDNS，避免重复初始化冲突
            ArduinoOTA.begin();
            String ipStr = WiFi.localIP().toString();
            Serial.printf("[OTA] Started on %s:%d (hostname: PowerStrip)\n", ipStr.c_str(), OTA_PORT);
            Serial.printf("[OTA] Access via: PowerStrip.local or %s\n", ipStr.c_str());
            ota_started = true;
        } else if (WiFi.getMode() & WIFI_AP) {
            ArduinoOTA.begin();
            String ipStr = WiFi.softAPIP().toString();
            Serial.printf("[OTA] Started in AP mode on %s:%d\n", ipStr.c_str(), OTA_PORT);
            Serial.println("[OTA] WARNING: mDNS not available in AP mode, use IP directly");
            ota_started = true;
        } else {
            static unsigned long last_warn = 0;
            if (millis() - last_warn > 5000) {
                Serial.println("[OTA] Waiting for WiFi before starting...");
                last_warn = millis();
            }
            return;
        }
    }
    ArduinoOTA.handle();
}