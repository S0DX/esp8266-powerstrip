#include "mqtt_mgr.h"
#include <EEPROM.h>
#include "config.h"
#include "gpio_mgr.h"
#include "sy7t609.h"
#include "energy_mgr.h"

#define EEPROM_MQTT_MAGIC_ADDR   136
#define EEPROM_MQTT_SERVER_ADDR  137
#define EEPROM_MQTT_PORT_ADDR    201
#define EEPROM_MQTT_USER_ADDR    203
#define EEPROM_MQTT_PASS_ADDR    235
#define EEPROM_MQTT_ENABLED_ADDR 267

WiFiClient MQTTManager::wifiClient_;
PubSubClient MQTTManager::client_(MQTTManager::wifiClient_);
MQTTConfig MQTTManager::config_;
unsigned long MQTTManager::last_reconnect_time_ = 0;
unsigned long MQTTManager::last_publish_time_ = 0;
static bool mqtt_publish_pending_ = false;

void MQTTManager::requestPublish() {
    mqtt_publish_pending_ = true;
}

void MQTTManager::init() {
    loadConfig();
    client_.setCallback(onMessage);
}

void MQTTManager::loadConfig() {
    if (EEPROM.read(EEPROM_MQTT_MAGIC_ADDR) != 0x55) {
        memset(&config_, 0, sizeof(config_));
        config_.enabled = false;
        config_.port = 1883;
        return;
    }
    
    config_.enabled = (EEPROM.read(EEPROM_MQTT_ENABLED_ADDR) == 1);
    
    int len;
    
    len = 0;
    for (int i=0; i<64; i++) {
        char c = EEPROM.read(EEPROM_MQTT_SERVER_ADDR + i);
        if (c >= 32 && c <= 126) config_.server[len++] = c;
        else break;
    }
    config_.server[len] = '\0';
    
    config_.port = (EEPROM.read(EEPROM_MQTT_PORT_ADDR) << 8) | EEPROM.read(EEPROM_MQTT_PORT_ADDR + 1);
    
    len = 0;
    for (int i=0; i<32; i++) {
        char c = EEPROM.read(EEPROM_MQTT_USER_ADDR + i);
        if (c >= 32 && c <= 126) config_.username[len++] = c;
        else break;
    }
    config_.username[len] = '\0';
    
    len = 0;
    for (int i=0; i<32; i++) {
        char c = EEPROM.read(EEPROM_MQTT_PASS_ADDR + i);
        if (c >= 32 && c <= 126) config_.password[len++] = c;
        else break;
    }
    config_.password[len] = '\0';
    

    
    if (config_.enabled && strlen(config_.server) > 0) {
        client_.setServer(config_.server, config_.port);
        Serial.printf("[MQTT] Config loaded. Server: %s:%d\n", config_.server, config_.port);
    }
}

void MQTTManager::saveConfig(const MQTTConfig& config) {
    config_ = config;
    EEPROM.write(EEPROM_MQTT_MAGIC_ADDR, 0x55);
    EEPROM.write(EEPROM_MQTT_ENABLED_ADDR, config.enabled ? 1 : 0);
    
    for (int i=0; i<64; i++) EEPROM.write(EEPROM_MQTT_SERVER_ADDR + i, config.server[i]);
    EEPROM.write(EEPROM_MQTT_PORT_ADDR, (config.port >> 8) & 0xFF);
    EEPROM.write(EEPROM_MQTT_PORT_ADDR + 1, config.port & 0xFF);
    for (int i=0; i<32; i++) EEPROM.write(EEPROM_MQTT_USER_ADDR + i, config.username[i]);
    for (int i=0; i<32; i++) EEPROM.write(EEPROM_MQTT_PASS_ADDR + i, config.password[i]);
    
    EEPROM.commit();
    Serial.println("[MQTT] Config saved.");
    
    client_.disconnect();
    if (config_.enabled && strlen(config_.server) > 0) {
        client_.setServer(config_.server, config_.port);
    }
}

MQTTConfig MQTTManager::getConfig() {
    return config_;
}

bool MQTTManager::isConnected() {
    return client_.connected();
}

void MQTTManager::connect() {
    if (!config_.enabled || strlen(config_.server) == 0) return;
    
    Serial.printf("[MQTT] Connecting to %s...\n", config_.server);
    String clientId = "PowerStrip-" + String(ESP.getChipId(), HEX);
    
    bool result = false;
    if (strlen(config_.username) > 0) {
        result = client_.connect(clientId.c_str(), config_.username, config_.password);
    } else {
        result = client_.connect(clientId.c_str());
    }
    
    if (result) {
        Serial.println("[MQTT] Connected!");
        client_.subscribe("PowerStrip/set/relay");
        publishStatus();
    } else {
        Serial.printf("[MQTT] Connect failed, rc=%d\n", client_.state());
    }
}

void MQTTManager::onMessage(char* topic, byte* payload, unsigned int length) {
    String msg = "";
    for (unsigned int i = 0; i < length; i++) {
        msg += (char)payload[i];
    }
    
    Serial.printf("[MQTT] Received on %s: %s\n", topic, msg.c_str());
    
    if (String(topic) == "PowerStrip/set/relay") {
        if (msg == "on") {
            GPIOManager::setRelays(true, true);
        } else if (msg == "off") {
            GPIOManager::setRelays(false, false);
        }
        publishStatus();
    }
}

void MQTTManager::publishStatus() {
    if (!client_.connected()) return;
    
    String payload = "{";
    payload += "\"relay_master\":" + String(GPIOManager::getRelayMaster() ? "true" : "false");
    payload += ",\"relay_slave\":" + String(GPIOManager::getRelaySlave() ? "true" : "false");
    if (SY7T609::isReady()) {
        payload += ",\"voltage\":" + String(SY7T609::getVoltage(), 1);
        payload += ",\"current\":" + String(SY7T609::getCurrent(), 3);
        payload += ",\"power\":" + String(SY7T609::getPower(), 1);
        payload += ",\"energy\":" + String(EnergyManager::getTotalEnergy(), 2);
    }
    payload += "}";
    
    client_.publish("PowerStrip/status", payload.c_str(), true);
}

void MQTTManager::handle() {
    if (!config_.enabled) return;
    
    if (WiFi.status() == WL_CONNECTED) {
        if (!client_.connected()) {
            if (millis() - last_reconnect_time_ > 30000) {
                last_reconnect_time_ = millis();
                connect();
            }
        } else {
            client_.loop();
            
            if (mqtt_publish_pending_) {
                mqtt_publish_pending_ = false;
                last_publish_time_ = millis();
                publishStatus();
            } else if (millis() - last_publish_time_ > 15000) {
                last_publish_time_ = millis();
                publishStatus();
            }
        }
    }
}
