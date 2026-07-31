#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP.h>
#include "config.h"
#include "gpio_mgr.h"
#include "button_mgr.h"
#include "wifi_mgr.h"
#include "ota_mgr.h"
#include "web_config.h"
#include "sy7t609.h"
#include "energy_mgr.h"
#include "mqtt_mgr.h"
#include "log_buffer.h"

void buttonCallback(ButtonHandler::ClickType type);
void handleButtonStateMachine();
void factoryReset();

void setup() {
    Serial.begin(115200);
    delayMicroseconds(100000);

    // 全局初始化 EEPROM，避免各模块重复 begin/end 造成地址冲突
    EEPROM.begin(1024);

    // 初始化内存日志缓冲区（不持久化，重启即清空）
    log_buffer_init();

    // 打印本次复位原因
    String reset_reason = ESP.getResetReason();
    DBG_PRINTF("[System] Reset reason: %s\n", reset_reason.c_str());
    DBG_PRINTF("[CrashLog] RTC available: %s\n", log_buffer_rtc_available() ? "yes" : "no");

    // 打印 RTC 崩溃日志区状态（调试用，无论复位原因）
    {
        uint32_t crash_magic = 0;
        int crash_count = 0;
        bool peek_ok = log_buffer_peek_crash_logs(&crash_magic, &crash_count);
        DBG_PRINTF("[CrashLog] RTC peek: ok=%s magic=0x%08X count=%d\n",
            peek_ok ? "yes" : "no", crash_magic, crash_count);

        // BOD 诊断：区分真实上电与电源跌落（BOD）
        // ESP8266 显示 "Power On" 的两种情况：
        //   1. 真实上电（Vcc 从 0 上升）→ RTC 被清空
        //   2. BOD 触发（Vcc 跌落到 2.8V 以下）→ RTC 被清空
        // 软件 WDT / 异常 / 软复位 → RTC 保留 → reset reason 不会是 "Power On"
        if (reset_reason.indexOf("Power") >= 0 || reset_reason.indexOf("power") >= 0) {
            if (crash_magic == 0xDEADBEEF) {
                DBG_PRINTF("[BOD-Diag] ANOMALY: Power-On reset but RTC crash log preserved\n");
                DBG_PRINTF("[BOD-Diag] This is unusual - possible SDK edge case or partial RTC retention\n");
            } else {
                DBG_PRINTF("[BOD-Diag] Power-On reset with RTC cleared\n");
                DBG_PRINTF("[BOD-Diag] Root cause: real power-cycle OR Brownout Detection (BOD) triggered\n");
                DBG_PRINTF("[BOD-Diag] If device was NOT physically power-cycled, BOD is the cause:\n");
                DBG_PRINTF("[BOD-Diag]   transient current spike sagged 3.3V below 2.8V threshold\n");
                DBG_PRINTF("[BOD-Diag]   Common culprits: WiFi TX + EEPROM commit + relay latch overlap\n");
            }
        }

        if (reset_reason.indexOf("Watchdog") >= 0 || reset_reason.indexOf("Exception") >= 0 || reset_reason.indexOf("Soft") >= 0) {
            char crash_logs[CRASH_LOG_MAX_LINES][CRASH_LOG_LINE_LEN];
            int crash_count_loaded = 0;
            if (log_buffer_load_crash_logs(crash_logs, CRASH_LOG_MAX_LINES, &crash_count_loaded)) {
                DBG_PRINTF("[CrashLog] Previous crash reason: %s\n", reset_reason.c_str());
                for (int i = 0; i < crash_count_loaded; i++) {
                    DBG_PRINTF("[CrashLog] %s\n", crash_logs[i]);
                }
            } else {
                DBG_PRINTF("[CrashLog] No valid logs found for previous crash\n");
            }
        }
    }

    DBG_PRINTF("\n=================================\n");

    GPIOManager::init();
    ESP.wdtFeed();

    ButtonHandler::init();
    ButtonHandler::setCallback(buttonCallback);
    ESP.wdtFeed();

    WiFiManager::init();
    ESP.wdtFeed();

    OTAManager::init("PowerStrip", "ota_password");
    ESP.wdtFeed();
    WebConfigServer::init();
    WebConfigServer::setSaveCallback([](const char* ssid, const char* password) {
        WiFiManager::saveConfig(ssid, password);
    });
    ESP.wdtFeed();

    SY7T609::init();
    ESP.wdtFeed();

    // 如果 SY7T609 已启用，Serial 被切换为 9600 波特率
    // 此时 g_meter_enabled = true，所有 DBG_PRINTF 输出到 Serial1（GPIO2）
    // Serial1 已在 sy7t609.cpp 的 initializeSY7T609() 中初始化
    if (g_meter_enabled) {
        DBG_PRINTF("[System] SY7T609 enabled, debug output switched to Serial1 (GPIO2)\n");
    }

    EnergyManager::init();
    ESP.wdtFeed();
    MQTTManager::init();
    ESP.wdtFeed();

    if (EEPROM.read(EEPROM_LAYOUT_VERSION_ADDR) != EEPROM_LAYOUT_VERSION) {
        EEPROM.write(EEPROM_LAYOUT_VERSION_ADDR, EEPROM_LAYOUT_VERSION);
        EEPROM.commit();
        DBG_PRINTF("[EEPROM] Layout version updated to 0x%02X\n", EEPROM_LAYOUT_VERSION);
    }

    DBG_PRINTF("[System] Setup complete\n");
}

void loop() {
    ESP.wdtFeed();  // 显式喂狗，防止任何 handle 内部偶发卡死触发看门狗
    static unsigned long last_print = 0;
    static bool wifi_scan_active = false;
    static unsigned long wifi_scan_active_since = 0;

    if (wifi_scan_active && millis() - wifi_scan_active_since > 30000) {
        wifi_scan_active = false;
        DBG_PRINTF("[WiFiScan] Stale scan lock cleared (timeout)\n");
    }

    WiFiManager::handle();
    ESP.wdtFeed();
    OTAManager::handle();
    ESP.wdtFeed();
    WebConfigServer::handle();
    ESP.wdtFeed();
    ButtonHandler::handle();
    handleButtonStateMachine();
    ESP.wdtFeed();
    GPIOManager::updateLEDs();
    GPIOManager::handleTimer();
    GPIOManager::handleEnablePulse();
    GPIOManager::flushPendingEEPROM();
    ESP.wdtFeed();
    // P0: Web 请求处理期间跳过 SY7T609 读取（避免 100ms 阻塞导致 TCP ACK 超时）
    if (!WebConfigServer::isWebRequestActive()) {
        SY7T609::handle();
    }
    ESP.wdtFeed();
    // P1: 处理 WiFi 发射功率 boost 超时恢复
    WiFiManager::handleTxPowerTimeout();
    EnergyManager::handle();
    ESP.wdtFeed();
    MQTTManager::handle();
    ESP.wdtFeed();

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
    if (GPIOManager::isBillingEnabled()) {
        bool shouldStop = false;
        GPIOManager::BillingStopReason stopReason = GPIOManager::BILLING_STOP_NONE;
        float usedEnergyWh = GPIOManager::getBillingUsedEnergy();
        float usedKwh = usedEnergyWh / 1000.0f;
        float thresholdKwh = GPIOManager::getBillingThreshold();
        float thresholdMoney = GPIOManager::getBillingThresholdMoney();
        uint16_t thresholdTime = GPIOManager::getBillingThresholdTime();

        // 能量阈值（阈值大于 0 才生效）
        if (thresholdKwh > 0.0f && usedKwh >= thresholdKwh) {
            shouldStop = true;
            stopReason = GPIOManager::BILLING_STOP_ENERGY;
        }
        // 金额阈值
        else if (thresholdMoney > 0.0f && GPIOManager::getBillingUsedMoney() >= thresholdMoney) {
            shouldStop = true;
            stopReason = GPIOManager::BILLING_STOP_MONEY;
        }
        // 时长阈值
        else if (thresholdTime > 0 && GPIOManager::getBillingUsedMinutes() >= thresholdTime) {
            shouldStop = true;
            stopReason = GPIOManager::BILLING_STOP_TIME;
        }

        if (shouldStop) {
            GPIOManager::setRelays(false, false);
            GPIOManager::stopBilling(stopReason);
            DBG_PRINTF("[Billing] Limit reached (reason=%d): used=%.2fWh, money=%.2f, time=%umin\n",
                (int)stopReason, usedEnergyWh, GPIOManager::getBillingUsedMoney(), GPIOManager::getBillingUsedMinutes());
        }
    }

    // WiFi 检测功能（人来开电）— 任何倒计时运行中完全暂停扫描（P0-C）
    // 倒计时结束后自动恢复，避免目标信号重现触发 setRelays 取消倒计时
    if (GPIOManager::isWiFiDetectEnabled()) {
        bool in_countdown = GPIOManager::isTimerRunning();
        static bool scan_in_progress = false;
        static unsigned long scan_start_time = 0;
        static unsigned long next_scan_time = 0;
        static uint8_t lost_count = 0;
        static bool target_present = false;
        unsigned long scanInterval = GPIOManager::getWiFiDetectScanInterval();

        if (!in_countdown && !scan_in_progress && millis() > next_scan_time && !wifi_scan_active) {
            if (WiFiManager::isConnected() && WiFi.status() == WL_CONNECTED) {
                next_scan_time = millis() + 30000;
                scan_start_time = millis();
                wifi_scan_active = true;
                wifi_scan_active_since = millis();
                // 人来上电无需探测隐藏 SSID，关闭后可大幅缩短扫描时长，降低页面卡顿
                ESP.wdtFeed();
                WiFi.scanNetworks(true, false);
                ESP.wdtFeed();
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
                    for (int i = 0; i < n && !found; i++) {
                        if ((i & 3) == 0) {
                            yield();
                            WebConfigServer::handle();
                        }
                        int8_t rssi = WiFi.RSSI(i);
                        if (rssi < rssiThreshold) continue;
                        if (WiFi.SSID(i) == target) {
                            found = true;
                            uint8_t* bssid = WiFi.BSSID(i);
                            if (bssid) {
                                char mac[18];
                                snprintf(mac, sizeof(mac), "%02X:%02X:%02X:%02X:%02X:%02X",
                                    bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5]);
                                foundMac = String(mac);
                                DBG_PRINTF("[WiFiDetect] Found '%s' RSSI:%d MAC:%s\n", target.c_str(), rssi, mac);
                            } else {
                                DBG_PRINTF("[WiFiDetect] Found '%s' RSSI:%d (no BSSID)\n", target.c_str(), rssi);
                            }
                        }
                    }
                }
                WiFi.scanDelete();
                GPIOManager::recordWiFiDetectHourly(found);

                if (found) {
                    lost_count = 0;
                    target_present = true;
                    if (!GPIOManager::isWiFiDetectPresent()) {
                        GPIOManager::setWiFiDetectPresent(true);
                        GPIOManager::setWiFiDetectMac(foundMac.c_str());
                        if (!in_countdown) {
                            // 首次检测到目标：设备在用电且开启联锁倒计时则启动快速倒计时
                            float power = (SY7T609::isEnabled() && SY7T609::isReady()) ? SY7T609::getPower() : 0.0f;
                            bool isUsingPower = (power > GPIOManager::getPowerOffThreshold());
                            if (isUsingPower && GPIOManager::isWiFiDetectLinkTimer()) {
                                GPIOManager::setTimerSource(1);
                                GPIOManager::setTimerEnabled(true);
                                GPIOManager::startTimer(GPIOManager::getWiFiDetectRelayOnlyMaster() ? 1 : 0);
                                DBG_PRINTF("[WiFiDetect] Target found, power in use, starting countdown %dmin\n", GPIOManager::getTimerDuration());
                            } else {
                                GPIOManager::setRelays(true, !GPIOManager::getWiFiDetectRelayOnlyMaster());
                                DBG_PRINTF("[WiFiDetect] Target found, relay ON (power=%.2fW, link=%s)\n",
                                    power, GPIOManager::isWiFiDetectLinkTimer() ? "yes" : "no");
                            }
                        } else {
                            DBG_PRINTF("[WiFiDetect] Target found but in countdown, suppressed\n");
                        }
                    } else {
                        GPIOManager::setWiFiDetectMac(foundMac.c_str());
                    }
                } else if (GPIOManager::isWiFiDetectPresent()) {
                    lost_count++;
                    target_present = false;
                    DBG_PRINTF("[WiFiDetect] Target not found, lost count: %d/2\n", lost_count);
                    if (lost_count >= 2) {
                        GPIOManager::setWiFiDetectPresent(false);
                        GPIOManager::setWiFiDetectMac("");
                        lost_count = 0;
                        if (GPIOManager::isWiFiDetectLinkTimer()) {
                            float power = (SY7T609::isEnabled() && SY7T609::isReady()) ? SY7T609::getPower() : 0.0f;
                            if (power > GPIOManager::getPowerOffThreshold()) {
                                GPIOManager::setTimerSource(1);
                                GPIOManager::setTimerEnabled(true);
                                GPIOManager::startTimer(GPIOManager::getWiFiDetectRelayOnlyMaster() ? 1 : 0);
                                DBG_PRINTF("[WiFiDetect] Target lost x2, power in use, starting countdown %dmin\n", GPIOManager::getTimerDuration());
                            } else {
                                GPIOManager::setRelays(false, false);
                                DBG_PRINTF("[WiFiDetect] Target lost x2, power low, relay OFF\n");
                            }
                        } else {
                            GPIOManager::setRelays(false, false);
                            DBG_PRINTF("[WiFiDetect] Target lost x2, relay OFF\n");
                        }
                    }
                }
                unsigned long cooldown = target_present ? 5000 : scanInterval + 15000;
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
        static bool btn_scan_in_countdown = false;

        if (!btn_scan_in_progress && !wifi_scan_active) {
            GPIOManager::clearButtonDetectScanRequested();
            btn_scan_in_progress = true;
            btn_scan_in_countdown = GPIOManager::isTimerRunning();
            btn_scan_start = millis();
            wifi_scan_active = true;
            wifi_scan_active_since = millis();
            // 按钮触发单次扫描同样无需探测隐藏 SSID，避免页面卡顿
            ESP.wdtFeed();
            WiFi.scanNetworks(true, false);
            ESP.wdtFeed();
            DBG_PRINTF("[ButtonDetect] One-shot scan started (in_countdown=%s)\n", btn_scan_in_countdown ? "yes" : "no");
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
                            if (bssid) {
                                char mac[18];
                                snprintf(mac, sizeof(mac), "%02X:%02X:%02X:%02X:%02X:%02X",
                                    bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5]);
                                GPIOManager::setWiFiDetectMac(mac);
                                DBG_PRINTF("[ButtonDetect] Found '%s' RSSI:%d MAC:%s\n", target.c_str(), rssi, mac);
                            } else {
                                GPIOManager::setWiFiDetectMac("");
                                DBG_PRINTF("[ButtonDetect] Found '%s' RSSI:%d (no BSSID)\n", target.c_str(), rssi);
                            }
                            break;
                        }
                    }
                }
                WiFi.scanDelete();

                if (found) {
                    GPIOManager::setWiFiDetectPresent(true);
                    if (!btn_scan_in_countdown) {
                        GPIOManager::setRelays(true, !GPIOManager::getWiFiDetectRelayOnlyMaster());
                        DBG_PRINTF("[ButtonDetect] Target found, relays ON\n");
                    } else {
                        DBG_PRINTF("[ButtonDetect] Target found but in countdown, suppressed\n");
                    }
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

    // 每 5 秒保存一次崩溃日志到 RTC（掉电丢失，软复位/看门狗复位保留）
    static unsigned long last_rtc_save = 0;
    if (millis() - last_rtc_save >= 5000) {
        last_rtc_save = millis();
        ESP.wdtFeed();
        if (!log_buffer_save_crash_logs()) {
            DBG_PRINTF("[CrashLog] RTC save failed\n");
        }
    }

    delay(1);
    ESP.wdtFeed();  // 循环末尾再次喂狗，确保 delay 期间也被刷新

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

enum ButtonFsmState {
    BTN_FSM_IDLE,        // 等待按钮
    BTN_FSM_PRESSED,     // 按钮按下中
    BTN_FSM_WAIT_SECOND  // 已开主继电器，等待 300ms 内第二次按下开从继电器
};

static ButtonFsmState g_btn_state = BTN_FSM_IDLE;
static unsigned long g_btn_press_start = 0;
static unsigned long g_btn_wait_start = 0;
static bool g_btn_second_press = false;
static bool g_btn_wait_open = false; // WAIT_SECOND 目标：true=开主继电器, false=关主继电器

void factoryReset() {
    DBG_PRINTF("[Button] Long press - factory reset\n");
    for (int i = 0; i < 1024; i++) {
        if (i == EEPROM_SY7T609_ENABLED_ADDR) {
            EEPROM.write(i, 1); // 电量检测默认开启
        } else {
            EEPROM.write(i, 0);
        }
    }
    EEPROM.commit();
    // 重新初始化默认配置，确保各模块状态一致
    GPIOManager::reset();
    WiFiManager::reset();
    EnergyManager::reset();
    delay(100);
    ESP.restart();
}

void buttonCallback(ButtonHandler::ClickType type) {
    unsigned long now = millis();

    // 人来上电启动的倒计时运行期间，短按物理按键可关闭此次倒计时
    if (type == ButtonHandler::CLICK_RELEASE &&
        GPIOManager::isTimerRunning() &&
        GPIOManager::getTimerSource() == 1) {
        DBG_PRINTF("[Button] Cancelling WiFi-detect countdown\n");
        GPIOManager::stopTimer();
        GPIOManager::setRelays(false, false);
        return;
    }

    // 锁定模式下，只允许长按关闭继电器；短按触发人来上电按键检测
    if (GPIOManager::isLocked()) {
        if (type == ButtonHandler::CLICK_PRESS) {
            g_btn_press_start = now;
        } else if (type == ButtonHandler::CLICK_RELEASE) {
            unsigned long duration = now - g_btn_press_start;
            if (duration >= BUTTON_LONG_PRESS_MS) {
                DBG_PRINTF("[Button] Long press in lock mode - turning off all relays\n");
                GPIOManager::setRelays(false, false);
            } else if (duration < BUTTON_SINGLE_CLICK_MS && GPIOManager::isButtonDetectEnabled()) {
                DBG_PRINTF("[Button] Button detect scan requested in lock mode\n");
                GPIOManager::setButtonDetectScanRequested();
            } else {
                DBG_PRINTF("[Button] Locked: ignoring physical button input (use long press to turn off)\n");
            }
        }
        return;
    }

    if (type == ButtonHandler::CLICK_PRESS) {
        g_btn_press_start = now;
        g_btn_second_press = false;

        switch (g_btn_state) {
            case BTN_FSM_IDLE:
                g_btn_state = BTN_FSM_PRESSED;
                break;

            case BTN_FSM_WAIT_SECOND:
                // 200ms 内第二次按下：打开从继电器
                DBG_PRINTF("[Button] Second press within window - enabling slave relay\n");
                g_btn_second_press = true;
                // 倒计时进行中则切换为 both 模式（主+从一同倒计时），不停止倒计时
                if (GPIOManager::isTimerRunning()) {
                    GPIOManager::setRelaySlaveDuringTimer();
                    GPIOManager::setTimerTarget(0);
                } else {
                    GPIOManager::setRelaySlave(true);
                }
                g_btn_state = BTN_FSM_PRESSED;
                break;

            case BTN_FSM_PRESSED:
                // 已在按下中，忽略（由状态机处理长按）
                break;
        }
    } else if (type == ButtonHandler::CLICK_RELEASE) {
        unsigned long duration = now - g_btn_press_start;

        switch (g_btn_state) {
            case BTN_FSM_PRESSED:
                if (g_btn_second_press) {
                    // 第二次按下的释放，回到 IDLE
                    g_btn_second_press = false;
                    g_btn_state = BTN_FSM_IDLE;
                } else if (duration >= BUTTON_LONG_PRESS_MS) {
                    // 长按已触发，释放后回到 IDLE
                    g_btn_state = BTN_FSM_IDLE;
                } else if (duration < BUTTON_SINGLE_CLICK_MS) {
                    // 短按释放
                    bool masterOn = GPIOManager::getRelayMaster();
                    bool slaveOn = GPIOManager::getRelaySlave();
                    if (!masterOn && !slaveOn) {
                        // 全关：立即开主继电器，进入等待双按窗口
                        DBG_PRINTF("[Button] First press - enabling master relay, waiting %dms for second press\n", BUTTON_DOUBLE_WINDOW_MS);
                        GPIOManager::setRelayMaster(true);
                        if (GPIOManager::isButtonAutoTimerEnabled() && GPIOManager::isTimerEnabled()) {
                            GPIOManager::setTimerSource(0);  // 手动源
                            GPIOManager::startTimer(1);      // master-only 倒计时
                        }
                        g_btn_wait_open = true;
                        g_btn_wait_start = now;
                        g_btn_state = BTN_FSM_WAIT_SECOND;
                    } else if (masterOn && !slaveOn) {
                        // 主开从关：延迟关闭主继电器，期间可双按开从继电器
                        DBG_PRINTF("[Button] First press - master on/slave off, waiting %dms for second press\n", BUTTON_DOUBLE_WINDOW_MS);
                        g_btn_wait_open = false;
                        g_btn_wait_start = now;
                        g_btn_state = BTN_FSM_WAIT_SECOND;
                    } else {
                        // 主从都开：直接关闭所有继电器
                        DBG_PRINTF("[Button] Press while both relays on - turning off all relays\n");
                        GPIOManager::setRelays(false, false);
                        g_btn_state = BTN_FSM_IDLE;
                    }
                } else {
                    // 按下时间过长，不算短按，忽略
                    g_btn_state = BTN_FSM_IDLE;
                }
                break;

            case BTN_FSM_IDLE:
            case BTN_FSM_WAIT_SECOND:
                // 不应到达此处，安全回到 IDLE
                g_btn_state = BTN_FSM_IDLE;
                break;
        }
    }
}

void handleButtonStateMachine() {
    static unsigned long last_check = 0;
    unsigned long now = millis();
    if (now - last_check < 20) return; // 50Hz 检查足够
    last_check = now;

    // 锁定模式下长按检测由 buttonCallback 处理，这里只处理非锁定状态
    if (!GPIOManager::isLocked() && g_btn_state == BTN_FSM_PRESSED) {
        if (now - g_btn_press_start >= BUTTON_LONG_PRESS_MS) {
            factoryReset();
            // factoryReset 会重启，不会继续执行
        }
    }

    // 等待第二次按下的窗口超时
    if (g_btn_state == BTN_FSM_WAIT_SECOND) {
        if (now - g_btn_wait_start >= BUTTON_DOUBLE_WINDOW_MS) {
            if (g_btn_wait_open) {
                DBG_PRINTF("[Button] Double-press window expired, keeping master relay on\n");
            } else {
                DBG_PRINTF("[Button] Double-press window expired, turning off master relay\n");
                GPIOManager::setRelayMaster(false);
            }
            g_btn_state = BTN_FSM_IDLE;
        }
    }
}
