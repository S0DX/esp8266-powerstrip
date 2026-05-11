#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <WiFiUDP.h>
#include "config.h"
#include "gpio_mgr.h"
#include "button_mgr.h"
#include "wifi_mgr.h"
#include "ota_mgr.h"
#include "web_config.h"
#include "sy7t609.h"
#include "energy_mgr.h"
#include "mqtt_mgr.h"

void buttonCallback(ButtonHandler::ClickType type);

void setup() {
    Serial.begin(115200);
    delayMicroseconds(100000);
    
    // 全局初始化 EEPROM，避免各模块重复 begin/end 造成地址冲突
    EEPROM.begin(512);

    DBG_PRINTF("\n=================================\n");
    DBG_PRINTF("PowerStrip Firmware v%s\n", VERSION);
    DBG_PRINTF("=================================\n");

    GPIOManager::init();

    ButtonHandler::init();
    ButtonHandler::setCallback(buttonCallback);

    WiFiManager::init();

    OTAManager::init("PowerStrip", "ota_password");
    WebConfigServer::init();
    WebConfigServer::setSaveCallback([](const char* ssid, const char* password) {
        WiFiManager::saveConfig(ssid, password);
    });

    SY7T609::init();

    // 如果 SY7T609 已启用，Serial 被切换为 9600 波特率
    // 此时 g_meter_enabled = true，所有 DBG_PRINTF 输出到 Serial1（GPIO2）
    // Serial1 已在 sy7t609.cpp 的 initializeSY7T609() 中初始化
    if (g_meter_enabled) {
        DBG_PRINTF("[System] SY7T609 enabled, debug output switched to Serial1 (GPIO2)\n");
    }

    EnergyManager::init();
    MQTTManager::init();

    DBG_PRINTF("[System] Setup complete\n");
}

void loop() {
    static unsigned long last_print = 0;
    static bool wifi_scan_active = false;
    static unsigned long wifi_scan_active_since = 0;
    static WiFiUDP discoveryUdp;
    static unsigned long last_discovery = 0;
    static bool discovery_initialized = false;

    if (wifi_scan_active && millis() - wifi_scan_active_since > 30000) {
        wifi_scan_active = false;
        DBG_PRINTF("[WiFiScan] Stale scan lock cleared (timeout)\n");
    }

    // UDP 设备发现广播 - 非阻塞，每 3 秒执行一次
    if (WiFi.status() == WL_CONNECTED) {
        if (!discovery_initialized) {
            discoveryUdp.begin(DISCOVERY_UDP_PORT);
            discovery_initialized = true;
        }
        if (millis() - last_discovery > DISCOVERY_INTERVAL_MS) {
            last_discovery = millis();
            IPAddress broadcastIP;
            broadcastIP.fromString(DISCOVERY_BROADCAST_IP);
            char buf[256];
            snprintf(buf, sizeof(buf),
                "{\"name\":\"PowerStrip\",\"ip\":\"%s\",\"mac\":\"%s\",\"ver\":\"%s\",\"hostname\":\"%s\"}",
                WiFi.localIP().toString().c_str(),
                WiFi.macAddress().c_str(),
                VERSION,
                GPIOManager::getMDNSHostname().c_str()
            );
            discoveryUdp.beginPacket(broadcastIP, DISCOVERY_UDP_PORT);
            discoveryUdp.write(buf);
            discoveryUdp.endPacket();
            // DBG_PRINTF("[Discovery] Broadcast sent: %s\n", buf);
        }
    }

    WiFiManager::handle();
    OTAManager::handle();
    WebConfigServer::handle();
    ButtonHandler::handle();
    GPIOManager::updateLEDs();
    GPIOManager::handleTimer();
    GPIOManager::handleEnablePulse();
    GPIOManager::flushPendingEEPROM();
    SY7T609::handle();
    EnergyManager::handle();
    MQTTManager::handle();

    if (GPIOManager::isPowerOffEnabled() && SY7T609::isReady()) {
        static int low_power_count = 0;
        static bool monitoring = false;
        float power = SY7T609::getPower();

        if (!monitoring && power > GPIOManager::getPowerOffThreshold()) {
            monitoring = true;
            low_power_count = 0;
        }

        if (monitoring) {
            if (power > 0 && power < GPIOManager::getPowerOffThreshold()) {
                low_power_count++;
                if (low_power_count >= 5) {
                    GPIOManager::setRelays(false, false);
                DBG_PRINTF("[PowerOff] Device removed, power %.2fW below %.2fW, relays OFF\n",
                        power, GPIOManager::getPowerOffThreshold());
                    monitoring = false;
                    low_power_count = 0;
                }
            } else if (power >= GPIOManager::getPowerOffThreshold()) {
                low_power_count = 0;
            } else if (power < 0.1f) {
                monitoring = false;
                low_power_count = 0;
            }
        }
    }

    // 计费供电检测（与拔除断电互斥）
    if (GPIOManager::isBillingEnabled() && SY7T609::isReady()) {
        float currentEnergy = EnergyManager::getTotalEnergy();
        float usedEnergy = currentEnergy - GPIOManager::getBillingStartEnergy();
        if (usedEnergy >= GPIOManager::getBillingThreshold() * 1000.0f) {
            GPIOManager::setRelays(false, false);
            GPIOManager::setBillingEnabled(false);
            DBG_PRINTF("[Billing] Energy limit reached: %.2f kWh, shutting down\n", usedEnergy / 1000.0f);
        }
    }

    // WiFi 检测功能（人来开电）— 倒计时中跳过扫描，避免信号重现导致重新计时
    if (GPIOManager::isWiFiDetectEnabled() && !(GPIOManager::isTimerRunning() && GPIOManager::getTimerSource() == 1)) {
        static bool scan_in_progress = false;
        static unsigned long scan_start_time = 0;
        static unsigned long next_scan_time = 0;
        static uint8_t lost_count = 0;
        static bool target_present = false;
        unsigned long scanInterval = GPIOManager::getWiFiDetectScanInterval();

        if (!scan_in_progress && millis() > next_scan_time && !wifi_scan_active) {
            if (WiFiManager::isConnected() && WiFi.status() == WL_CONNECTED) {
                next_scan_time = millis() + 30000;
                scan_start_time = millis();
                wifi_scan_active = true;
                wifi_scan_active_since = millis();
                WiFi.scanNetworks(true, true);
                scan_in_progress = true;
                DBG_PRINTF("[WiFiDetect] Async scan started\n");
            }
        }

        if (scan_in_progress) {
            int8_t scanResult = WiFi.scanComplete();
            if (scanResult >= 0) {
                scan_in_progress = false;
                wifi_scan_active = false;
                int n = scanResult;
                bool found = false;
                String target = GPIOManager::getWiFiDetectTarget();
                int8_t rssiThreshold = GPIOManager::getWiFiDetectRssiThreshold();
                String foundMac = "";
                if (target.length() > 0 && n > 0) {
                    for (int i = 0; i < n; i++) {
                        yield();
                        WebConfigServer::handle();
                        int8_t rssi = WiFi.RSSI(i);
                        if (rssi < rssiThreshold) continue;
                        if (WiFi.SSID(i) == target) {
                            found = true;
                            uint8_t* bssid = WiFi.BSSID(i);
                            char mac[18];
                            snprintf(mac, sizeof(mac), "%02X:%02X:%02X:%02X:%02X:%02X",
                                bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5]);
                            foundMac = String(mac);
                            DBG_PRINTF("[WiFiDetect] Found '%s' RSSI:%d MAC:%s\n", target.c_str(), rssi, mac);
                            break;
                        }
                    }
                }
                WiFi.scanDelete();

                if (found) {
                    lost_count = 0;
                    target_present = true;
                    if (!GPIOManager::isWiFiDetectPresent()) {
                        GPIOManager::setWiFiDetectPresent(true);
                        GPIOManager::setWiFiDetectMac(foundMac.c_str());
                        GPIOManager::setRelays(true, !GPIOManager::getWiFiDetectRelayOnlyMaster());
                        DBG_PRINTF("[WiFiDetect] Target found, relay ON\n");
                    } else {
                        GPIOManager::setWiFiDetectMac(foundMac.c_str());
                    }
                } else if (GPIOManager::isWiFiDetectPresent()) {
                    lost_count++;
                    target_present = false;
                    DBG_PRINTF("[WiFiDetect] Target not found, lost count: %d/3\n", lost_count);
                    if (lost_count >= 3) {
                        GPIOManager::setWiFiDetectPresent(false);
                        GPIOManager::setWiFiDetectMac("");
                        lost_count = 0;
                        
                        float power = SY7T609::isEnabled() ? SY7T609::getPower() : 0;
                        bool isUsingPower = (power > GPIOManager::getPowerOffThreshold());
                        DBG_PRINTF("[WiFiDetect] Target lost x3, current power: %.2fW, using: %s\n", 
                            power, isUsingPower ? "yes" : "no");
                        
                        if (isUsingPower && GPIOManager::isWiFiDetectLinkTimer()) {
                            if (GPIOManager::getTimerDuration() == 0) {
                                GPIOManager::setTimerDuration(1);
                            }
                            GPIOManager::setTimerSource(1);
                            GPIOManager::setTimerEnabled(true);
                            GPIOManager::startTimer();
                            DBG_PRINTF("[WiFiDetect] Power in use, starting countdown timer\n");
                        } else {
                            GPIOManager::setRelays(false, false);
                            DBG_PRINTF("[WiFiDetect] No power use, relay OFF\n");
                        }
                    }
                }
                unsigned long cooldown = target_present ? 60000 : scanInterval + 15000;
                next_scan_time = millis() + cooldown;
            } else if (millis() - scan_start_time > 15000) {
                scan_in_progress = false;
                wifi_scan_active = false;
                WiFi.scanDelete();
                WebConfigServer::handle();
                next_scan_time = millis() + scanInterval;
                DBG_PRINTF("[WiFiDetect] Scan timeout\n");
            }
        }
    }

    // 按键检测功能（锁定模式下物理按钮触发单次WiFi扫描）
    if (GPIOManager::isButtonDetectScanRequested()) {
        static bool btn_scan_in_progress = false;
        static unsigned long btn_scan_start = 0;

        if (!btn_scan_in_progress && !wifi_scan_active) {
            GPIOManager::clearButtonDetectScanRequested();
            btn_scan_in_progress = true;
            btn_scan_start = millis();
            wifi_scan_active = true;
            wifi_scan_active_since = millis();
            WiFi.scanNetworks(true, true);
            DBG_PRINTF("[ButtonDetect] One-shot scan started\n");
        }

        if (btn_scan_in_progress) {
            int8_t scanResult = WiFi.scanComplete();
            if (scanResult >= 0) {
                btn_scan_in_progress = false;
                wifi_scan_active = false;
                int n = scanResult;
                bool found = false;
                String target = GPIOManager::getWiFiDetectTarget();
                int8_t rssiThreshold = GPIOManager::getWiFiDetectRssiThreshold();

                if (target.length() > 0 && n > 0) {
                    for (int i = 0; i < n; i++) {
                        yield();
                        WebConfigServer::handle();
                        int8_t rssi = WiFi.RSSI(i);
                        if (rssi < rssiThreshold) continue;
                        if (WiFi.SSID(i) == target) {
                            found = true;
                            uint8_t* bssid = WiFi.BSSID(i);
                            char mac[18];
                            snprintf(mac, sizeof(mac), "%02X:%02X:%02X:%02X:%02X:%02X",
                                bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5]);
                            GPIOManager::setWiFiDetectMac(mac);
                            DBG_PRINTF("[ButtonDetect] Found '%s' RSSI:%d MAC:%s\n", target.c_str(), rssi, mac);
                            break;
                        }
                    }
                }
                WiFi.scanDelete();

                if (found) {
                    GPIOManager::setWiFiDetectPresent(true);
                    GPIOManager::setRelays(true, !GPIOManager::getWiFiDetectRelayOnlyMaster());
                    DBG_PRINTF("[ButtonDetect] Target found, relays ON\n");
                } else {
                    DBG_PRINTF("[ButtonDetect] Target not found\n");
                }
            } else if (millis() - btn_scan_start > 15000) {
                btn_scan_in_progress = false;
                wifi_scan_active = false;
                WiFi.scanDelete();
                WebConfigServer::handle();
                DBG_PRINTF("[ButtonDetect] Scan timeout\n");
            }
        }
    }

    delayMicroseconds(100);

    if (millis() - last_print > 3000) {
        last_print = millis();

        if (!SY7T609::isEnabled()) {
            DBG_PRINTF("[Meter] Disabled, enable via Web UI\n");
        } else if (SY7T609::isReady()) {
            DBG_PRINTF("--- Meter Data ---\n");
            DBG_PRINTF("Voltage:    %.1f V\n", SY7T609::getVoltage());
            DBG_PRINTF("Current:    %.3f A\n", SY7T609::getCurrent());
            DBG_PRINTF("Power:      %.2f W\n", SY7T609::getPower());
            DBG_PRINTF("PF:         %.3f\n", SY7T609::getPowerFactor());
            DBG_PRINTF("Frequency:  %.0f Hz\n", SY7T609::getFrequency());
            DBG_PRINTF("Temperature: %.1f C\n", SY7T609::getTemperature());
            DBG_PRINTF("Energy:     %.2f Wh\n", EnergyManager::getTotalEnergy());
            DBG_PRINTF("Relay: Master=%d Slave=%d\n",
                GPIOManager::getRelayMaster() ? 1 : 0,
                GPIOManager::getRelaySlave() ? 1 : 0);
            DBG_PRINTF("WiFi: %s (RSSI: %d dBm)\n",
                WiFiManager::getCurrentSSID().c_str(),
                WiFiManager::getCurrentRSSI());
            DBG_PRINTF("------------------\n");
        } else {
            DBG_PRINTF("[Meter] Waiting for data...\n");
        }
    }
}

void buttonCallback(ButtonHandler::ClickType type) {
    DBG_PRINTF("[Button] Click type: %d\n", type);

    // 锁定模式下，只允许长按关闭继电器
    if (GPIOManager::isLocked()) {
        if (type == ButtonHandler::CLICK_LONG) {
            DBG_PRINTF("[Button] Long press in lock mode - turning off all relays\n");
            GPIOManager::setRelays(false, false);
        } else if (type == ButtonHandler::CLICK_SINGLE && GPIOManager::isButtonDetectEnabled()) {
            DBG_PRINTF("[Button] Button detect scan requested in lock mode\n");
            GPIOManager::setButtonDetectScanRequested();
        } else {
            DBG_PRINTF("[Button] Locked: ignoring physical button input (use long press to turn off)\n");
        }
        return;
    }

    switch (type) {
        case ButtonHandler::CLICK_SINGLE: {
            bool masterOn = GPIOManager::getRelayMaster();
            if (masterOn) {
                GPIOManager::setRelays(false, false);
            } else {
                GPIOManager::setRelayMaster(true);
                if (GPIOManager::isButtonAutoTimerEnabled() && GPIOManager::isTimerEnabled()) {
                    GPIOManager::startTimer();
                }
            }
            break;
        }

        case ButtonHandler::CLICK_DOUBLE:
            GPIOManager::setRelays(true, true);
            if (GPIOManager::isButtonAutoTimerEnabled() && GPIOManager::isTimerEnabled()) {
                GPIOManager::startTimer();
            }
            break;

        case ButtonHandler::CLICK_LONG:
            DBG_PRINTF("[Button] Long press - factory reset\n");
            for (int i = 0; i < 512; i++) {
                EEPROM.write(i, 0);
            }
            EEPROM.commit();
            delay(100);
            ESP.restart();
            break;
    }
}
