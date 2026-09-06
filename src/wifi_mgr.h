#ifndef WIFI_MGR_H
#define WIFI_MGR_H

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>

class WiFiManager {
public:
    static void init();
    static void handle();
    static bool isConnected();
    static String getAPSuffix();
    static void setAPSuffix(const char* suffix);
    static String getHostname();
    static bool setHostname(const char* hostname);
    static void resetHostname();
    static void saveConfig(const char* ssid, const char* password);
    static bool loadConfig();
    static void reset();
    static void saveAPPassword(const char* password);
    static String getAPPassword();

    enum WiFiState {
        STATE_IDLE,
        STATE_CONNECTING,
        STATE_CONNECTED,
        STATE_DISCONNECTED
    };
    static String getCurrentSSID();
    static int getCurrentRSSI();
    static String getMDNSHostname();
    static void updateMDNS();  // 外部主循环调用，确保 Web 请求间隙也能响应 mDNS 查询

    // STA 连接成功后自动关闭 AP 开关
    static bool isAutoCloseAPEnabled();
    static void setAutoCloseAP(bool enabled);

    // WiFi 发射功率限制（降低峰值电流与功耗）
    static bool isWiFiTxPowerLimited();
    static void setWiFiTxPowerLimited(bool enabled);

    // P1: Web 请求时临时提升发射功率，改善弱信号场景页面加载
    static void boostTxPower();
    static void handleTxPowerTimeout();
    static float getCurrentTxPower();
    static bool isTxPowerBoosted();

private:
    static WiFiState state_;
    static unsigned long connection_start_time_;
    static unsigned long last_reconnect_attempt_;
    static int connection_failures_;
    static bool is_connecting_;
    static bool sta_connect_pending_;
    static bool mdns_started_;
    static bool auto_close_ap_;
    static bool ap_closed_by_auto_;
    static bool tx_power_limited_;
    static unsigned long sta_connected_time_;
    static unsigned long last_sleep_update_;
    static WiFiSleepType current_sleep_mode_;
    // P1: 动态功率提升状态
    static float current_tx_power_;        // 当前实际发射功率
    static unsigned long power_boost_until_;  // boost 截止时间（0=未提升）
    static void buildAPSSID();
    static void startMDNS();
    static void updateSleepMode();
    static void stopAP();
    static void restartAP();
};

#endif