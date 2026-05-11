#include <Arduino.h>
#include <ESP8266WiFi.h>
#include "config.h"
#include "gpio_mgr.h"
#include "button_mgr.h"
#include "wifi_mgr.h"
#include "ota_mgr.h"
#include "web_config.h"
#include "sy7t609.h"
#include "energy_mgr.h"

void buttonCallback(ButtonHandler::ClickType type);

void setup() {
    Serial.begin(115200);
    delay(100);
    Serial.println();
    Serial.println("=================================");
    Serial.printf("PowerStrip Firmware v%s\n", VERSION);
    Serial.println("=================================");

    GPIOManager::init();

    ButtonHandler::init();
    ButtonHandler::setCallback(buttonCallback);

    WiFiManager::init();

    OTAManager::init("PowerStrip", "ota_password");

    WebConfigServer::init();
    WebConfigServer::setSaveCallback([](const char* ssid, const char* password) {
        WiFiManager::saveConfig(ssid, password);
    });

    SY7T609::init(13, 2);

    EnergyManager::init();

    Serial.println("[System] Setup complete");
}

void loop() {
    static unsigned long last_print = 0;

    WiFiManager::handle();
    OTAManager::handle();
    WebConfigServer::handle();
    ButtonHandler::handle();
    GPIOManager::updateLEDs();
    SY7T609::handle();
    EnergyManager::handle();

    if (millis() - last_print > 5000) {
        last_print = millis();

        if (SY7T609::isReady()) {
            Serial.println("--- Meter Data ---");
            Serial.printf("Voltage:    %.1f V\n", SY7T609::getVoltage());
            Serial.printf("Current:    %.3f A\n", SY7T609::getCurrent());
            Serial.printf("Power:      %.2f W\n", SY7T609::getPower());
            Serial.printf("PF:         %.3f\n", SY7T609::getPowerFactor());
            Serial.printf("Frequency:  %.0f Hz\n", SY7T609::getFrequency());
            Serial.printf("Temperature: %.1f C\n", SY7T609::getTemperature());
            Serial.printf("Energy:     %.2f Wh\n", EnergyManager::getTotalEnergy());
            Serial.printf("Relay: Master=%d Slave=%d\n",
                GPIOManager::getRelayMaster() ? 1 : 0,
                GPIOManager::getRelaySlave() ? 1 : 0);
            Serial.printf("WiFi: %s (RSSI: %d dBm)\n",
                WiFiManager::getCurrentSSID().c_str(),
                WiFiManager::getCurrentRSSI());
            Serial.println("------------------");
        } else {
            Serial.println("[Meter] Not ready yet...");
        }
    }
}

void buttonCallback(ButtonHandler::ClickType type) {
    Serial.printf("[Button] Click type: %d\n", type);

    switch (type) {
        case ButtonHandler::CLICK_SINGLE:
            GPIOManager::setRelayMaster(!GPIOManager::getRelayMaster());
            break;

        case ButtonHandler::CLICK_DOUBLE:
            GPIOManager::setRelaySlave(!GPIOManager::getRelaySlave());
            break;

        case ButtonHandler::CLICK_LONG:
            Serial.println("[Button] Long press - factory reset");
            WiFiManager::reset();
            EnergyManager::reset();
            SY7T609::resetEnergy();
            GPIOManager::setRelayMaster(false);
            ESP.restart();
            break;
    }
}
