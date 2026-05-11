#include "wifi_mgr.h"
#include "config.h"
#include "gpio_mgr.h"
#include <EEPROM.h>

WiFiManager::WiFiState WiFiManager::state_ = WiFiManager::STATE_IDLE;
unsigned long WiFiManager::connection_start_time_ = 0;
unsigned long WiFiManager::last_reconnect_attempt_ = 0;
int WiFiManager::connection_failures_ = 0;
bool WiFiManager::is_connecting_ = false;

#define EEPROM_SSID_ADDR     0
#define EEPROM_PASS_ADDR     64
#define EEPROM_MAGIC         0xAB
#define EEPROM_MAGIC_ADDR    254
#define EEPROM_SIZE          256
#define WIFI_CONNECT_TIMEOUT 15000
#define RECONNECT_INTERVAL   10000
#define MAX_RECONNECT_ATTEMPTS 3

void WiFiManager::init() {
    EEPROM.begin(EEPROM_SIZE);

    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP("PowerStrip", NULL);

    Serial.printf("[WiFi] AP started: %s\n", WiFi.softAPIP().toString().c_str());

    if (loadConfig()) {
        Serial.printf("[WiFi] Loaded config for SSID: %s\n", WiFi.SSID().c_str());
        state_ = STATE_CONNECTING;
        connection_start_time_ = millis();
        is_connecting_ = true;
        WiFi.begin(WiFi.SSID().c_str(), WiFi.psk().c_str());
    } else {
        Serial.println("[WiFi] No saved config");
        state_ = STATE_IDLE;
    }
}

void WiFiManager::handle() {
    switch (state_) {
        case STATE_CONNECTING:
            if (WiFi.status() == WL_CONNECTED) {
                state_ = STATE_CONNECTED;
                is_connecting_ = false;
                connection_failures_ = 0;
                Serial.printf("[WiFi] Connected! IP: %s\n", WiFi.localIP().toString().c_str());
                GPIOManager::setBlueMode(GPIOManager::LED_ON);
                GPIOManager::setRedMode(GPIOManager::LED_OFF);
            } else if (millis() - connection_start_time_ > WIFI_CONNECT_TIMEOUT) {
                Serial.println("[WiFi] Connection timeout");
                is_connecting_ = false;
                connection_failures_++;
                if (connection_failures_ >= MAX_RECONNECT_ATTEMPTS) {
                    Serial.println("[WiFi] Max reconnect attempts reached, giving up");
                    state_ = STATE_IDLE;
                    GPIOManager::setRedMode(GPIOManager::LED_BLINK);
                } else {
                    Serial.printf("[WiFi] Will retry... attempt %d/%d\n", connection_failures_ + 1, MAX_RECONNECT_ATTEMPTS);
                    state_ = STATE_DISCONNECTED;
                    last_reconnect_attempt_ = millis();
                }
            }
            break;

        case STATE_CONNECTED:
            if (WiFi.status() != WL_CONNECTED) {
                state_ = STATE_DISCONNECTED;
                last_reconnect_attempt_ = millis();
                Serial.println("[WiFi] Disconnected!");
                GPIOManager::setRedMode(GPIOManager::LED_BLINK);
                GPIOManager::setBlueMode(GPIOManager::LED_OFF);
            }
            break;

        case STATE_DISCONNECTED:
            if (WiFi.status() == WL_CONNECTED) {
                state_ = STATE_CONNECTED;
                connection_failures_ = 0;
                GPIOManager::setBlueMode(GPIOManager::LED_ON);
                GPIOManager::setRedMode(GPIOManager::LED_OFF);
                Serial.printf("[WiFi] Reconnected! IP: %s\n", WiFi.localIP().toString().c_str());
            } else if (millis() - last_reconnect_attempt_ > RECONNECT_INTERVAL) {
                if (connection_failures_ < MAX_RECONNECT_ATTEMPTS) {
                    Serial.printf("[WiFi] Attempting reconnect %d/%d...\n", connection_failures_ + 1, MAX_RECONNECT_ATTEMPTS);
                    state_ = STATE_CONNECTING;
                    connection_start_time_ = millis();
                    is_connecting_ = true;
                    WiFi.begin();
                } else {
                    state_ = STATE_IDLE;
                    Serial.println("[WiFi] Max attempts reached, staying in IDLE mode");
                }
            }
            break;

        case STATE_IDLE:
            if (WiFi.status() == WL_CONNECTED) {
                state_ = STATE_CONNECTED;
                connection_failures_ = 0;
                GPIOManager::setBlueMode(GPIOManager::LED_ON);
                GPIOManager::setRedMode(GPIOManager::LED_OFF);
            }
            break;

        default:
            break;
    }
}

bool WiFiManager::isConnected() {
    return state_ == STATE_CONNECTED;
}

bool WiFiManager::isConfigMode() {
    return true;
}

void WiFiManager::enterConfigMode() {
}

void WiFiManager::exitConfigMode() {
}

void WiFiManager::saveConfig(const char* ssid, const char* password) {
    for (int i = 0; i < 32; i++) {
        EEPROM.write(EEPROM_SSID_ADDR + i, (i < strlen(ssid)) ? ssid[i] : 0);
    }
    for (int i = 0; i < 64; i++) {
        EEPROM.write(EEPROM_PASS_ADDR + i, (i < strlen(password)) ? password[i] : 0);
    }
    EEPROM.write(EEPROM_MAGIC_ADDR, EEPROM_MAGIC);
    EEPROM.commit();

    Serial.printf("[WiFi] Config saved: %s\n", ssid);
}

bool WiFiManager::loadConfig() {
    if (EEPROM.read(EEPROM_MAGIC_ADDR) != EEPROM_MAGIC) {
        return false;
    }

    char ssid[33] = {0};
    for (int i = 0; i < 32; i++) {
        ssid[i] = EEPROM.read(EEPROM_SSID_ADDR + i);
    }

    if (strlen(ssid) == 0) {
        return false;
    }

    return true;
}

void WiFiManager::reset() {
    EEPROM.write(EEPROM_MAGIC_ADDR, 0);
    EEPROM.commit();
    WiFi.disconnect();
    Serial.println("[WiFi] Config reset");
}

void WiFiManager::scanNetworks() {
    Serial.println("[WiFi] Scanning networks...");
    WiFi.scanNetworks(true);
}

int WiFiManager::getScanCount() {
    return WiFi.scanComplete();
}

String WiFiManager::getScanSSID(int i) {
    return WiFi.SSID(i);
}

int WiFiManager::getScanRSSI(int i) {
    return WiFi.RSSI(i);
}

bool WiFiManager::isScanEncrypted(int i) {
    return WiFi.encryptionType(i) != ENC_TYPE_NONE;
}

void WiFiManager::connectTo(int index) {
    int8_t scanStatus = WiFi.scanComplete();
    if (scanStatus <= 0 || index >= scanStatus) {
        Serial.println("[WiFi] Invalid scan index");
        return;
    }

    String ssid = WiFi.SSID(index);
    int encryption = WiFi.encryptionType(index);
    bool isEncrypted = (encryption != ENC_TYPE_NONE);

    Serial.printf("[WiFi] Connecting to: %s (encrypted: %d)\n", ssid.c_str(), isEncrypted);

    if (isEncrypted) {
        Serial.println("[WiFi] Password required, use connect with password");
    } else {
        WiFi.begin(ssid.c_str());
    }

    saveConfig(ssid.c_str(), "");
    state_ = STATE_CONNECTING;
    connection_start_time_ = millis();
    is_connecting_ = true;
    connection_failures_ = 0;
}

void WiFiManager::disconnect() {
    WiFi.disconnect();
    state_ = STATE_IDLE;
}

int WiFiManager::getConnectionFailures() {
    return connection_failures_;
}

void WiFiManager::resetConnectionFailures() {
    connection_failures_ = 0;
}

WiFiManager::WiFiState WiFiManager::getState() {
    return state_;
}

String WiFiManager::getCurrentSSID() {
    if (WiFi.status() == WL_CONNECTED) {
        return WiFi.SSID();
    }
    return "";
}

int WiFiManager::getCurrentRSSI() {
    if (WiFi.status() == WL_CONNECTED) {
        return WiFi.RSSI();
    }
    return 0;
}
