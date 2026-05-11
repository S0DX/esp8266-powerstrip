#ifndef WIFI_MGR_H
#define WIFI_MGR_H

#include <Arduino.h>
#include <ESP8266WiFi.h>

class WiFiManager {
public:
    static void init();
    static void handle();
    static bool isConnected();
    static bool isConfigMode();
    static void enterConfigMode();
    static void exitConfigMode();
    static void saveConfig(const char* ssid, const char* password);
    static bool loadConfig();
    static void reset();
    static void scanNetworks();
    static int getScanCount();
    static String getScanSSID(int i);
    static int getScanRSSI(int i);
    static bool isScanEncrypted(int i);
    static void connectTo(int index);
    static void disconnect();
    static int getConnectionFailures();
    static void resetConnectionFailures();

    enum WiFiState {
        STATE_IDLE,
        STATE_CONNECTING,
        STATE_CONNECTED,
        STATE_DISCONNECTED
    };
    static WiFiState getState();
    static String getCurrentSSID();
    static int getCurrentRSSI();

private:
    static WiFiState state_;
    static unsigned long connection_start_time_;
    static unsigned long last_reconnect_attempt_;
    static int connection_failures_;
    static bool is_connecting_;
};

#endif
