#ifndef MQTT_MGR_H
#define MQTT_MGR_H

#include <Arduino.h>
#include <PubSubClient.h>
#include <ESP8266WiFi.h>

struct MQTTConfig {
    char server[64];
    uint16_t port;
    char username[32];
    char password[32];
    bool enabled;
};

class MQTTManager {
public:
    static void init();
    static void handle();
    static void saveConfig(const MQTTConfig& config);
    static MQTTConfig getConfig();
    static bool isConnected();

    // 发送数据函数
    static void publishStatus();
    static void requestPublish();

private:
    static void connect();
    static void onMessage(char* topic, byte* payload, unsigned int length);
    static void loadConfig();

    static WiFiClient wifiClient_;
    static PubSubClient client_;
    static MQTTConfig config_;
    static unsigned long last_reconnect_time_;
    static unsigned long last_publish_time_;
};

#endif
