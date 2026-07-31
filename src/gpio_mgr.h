#ifndef GPIO_MGR_H
#define GPIO_MGR_H

#include <Arduino.h>
#include <EEPROM.h>

class GPIOManager {
public:
    static void init();
    static void reset();
    static void initRelayPins();
    static void initLEDPins();
    static void initButtonPin();

    static void setRelayMaster(bool on);
    static void setRelaySlave(bool on);
    // 倒计时运行中安全打开从继电器，不停止倒计时
    static void setRelaySlaveDuringTimer();
    static void setRelays(bool master, bool slave);
    static bool getRelayMaster();
    static bool getRelaySlave();

    static void setLocked(bool locked);
    static bool isLocked();

    static void triggerRelayEnable();

    static void setLEDBlue(bool on, bool blink = false);
    static void setLEDRed(bool on, bool blink = false);
    static void setLEDWhite(bool on);

    static void updateLEDs();

    // 定时器功能
    static void setTimerEnabled(bool enabled);
    static bool isTimerEnabled();
    static void setTimerDuration(uint16_t minutes);
    static uint16_t getTimerDuration();
    // target: 0=both relays, 1=master-only
    static void startTimer(uint8_t target = 0);
    static void stopTimer();
    static void setTimerTarget(uint8_t target);
    static bool isTimerRunning();
    static unsigned long getTimerRemaining();
    static void handleTimer();

    // 24小时循环功能
    static void setCycleEnabled(bool enabled);
    static bool isCycleEnabled();
    static void setCycleTime(uint8_t start_h, uint8_t start_m, uint8_t end_h, uint8_t end_m);
    static void getCycleTime(uint8_t& start_h, uint8_t& start_m, uint8_t& end_h, uint8_t& end_m);
    static void setCyclePeriod(uint8_t idx, uint8_t sh, uint8_t sm, uint8_t eh, uint8_t em);
    static bool getCyclePeriod(uint8_t idx, uint8_t& sh, uint8_t& sm, uint8_t& eh, uint8_t& em);
    static uint8_t getCyclePeriodCount();
    static bool addCyclePeriod(uint8_t sh, uint8_t sm, uint8_t eh, uint8_t em);
    static bool removeCyclePeriod(uint8_t idx);

    // 设备拔除断电功能
    static void setPowerOffEnabled(bool enabled);
    static bool isPowerOffEnabled();
    static void setPowerOffThreshold(float threshold);
    static float getPowerOffThreshold();

    // 计费供电功能（用电量/金额/时长阈值关闭）
    enum BillingMode { BILLING_ENERGY = 0, BILLING_MONEY = 1, BILLING_TIME = 2 };
    enum BillingStopReason { BILLING_STOP_NONE = 0, BILLING_STOP_ENERGY = 1, BILLING_STOP_MONEY = 2, BILLING_STOP_TIME = 3, BILLING_STOP_MANUAL = 4 };
    struct BillingRecord {
        uint32_t startTime;
        float usedEnergyWh;
        uint16_t costCents;
    };

    static void setBillingEnabled(bool enabled);
    static bool isBillingEnabled();
    static void setBillingMode(BillingMode mode);
    static BillingMode getBillingMode();
    static void setBillingThreshold(float kwh);
    static float getBillingThreshold();
    static void setBillingThresholdMoney(float yuan);
    static float getBillingThresholdMoney();
    static void setBillingThresholdTime(uint16_t minutes);
    static uint16_t getBillingThresholdTime();
    static float getBillingStartEnergy();
    static void setBillingStartEnergy(float energy);
    static BillingStopReason getBillingStopReason();
    static void setBillingStopReason(BillingStopReason reason);
    static float getBillingUsedEnergy();
    static float getBillingUsedMoney();
    static uint32_t getBillingUsedMinutes();
    static float getEnergyPrice();
    static void setEnergyPrice(float price);
    static void stopBilling(BillingStopReason reason);
    static void recordBillingHistory();
    static uint8_t getBillingHistoryCount();
    static BillingRecord getBillingHistory(uint8_t idx);

    // WiFi 检测功能（人来开电）
    static void setWiFiDetectEnabled(bool enabled);
    static bool isWiFiDetectEnabled();
    static void setWiFiDetectTarget(const char* ssid);
    static String getWiFiDetectTarget();
    static void setWiFiDetectTimeout(uint16_t seconds);
    static uint16_t getWiFiDetectTimeout();
    static void setWiFiDetectLinkTimer(bool linked);
    static bool isWiFiDetectLinkTimer();
    static void setWiFiDetectPresent(bool present);
    static bool isWiFiDetectPresent();
    static void setWiFiDetectRssiThreshold(int8_t rssi);
    static int8_t getWiFiDetectRssiThreshold();
    static void setWiFiDetectScanSpeed(uint8_t speed);  // 0=slow(30s), 1=medium(15s), 2=fast(5s), 3=auto
    static uint8_t getWiFiDetectScanSpeed();
    static unsigned long getWiFiDetectScanInterval();
    static unsigned long getWiFiDetectAutoInterval();
    static void recordWiFiDetectHourly(bool found);
    static float getWiFiDetectHourlyProbability();
    static void setWiFiDetectMac(const char* mac);
    static String getWiFiDetectMac();
    static bool isWiFiDetectPowerChecking();
    static void setWiFiDetectRelayOnlyMaster(bool only);
    static bool getWiFiDetectRelayOnlyMaster();
    static void setTimerSource(uint8_t source);  // 0=manual, 1=wifi_detect
    static uint8_t getTimerSource();
    static bool isButtonAutoTimerEnabled();
    static void setButtonAutoTimerEnabled(bool en);
    static void requestEEPROMCommit();
    static void flushPendingEEPROM();
    static void handleEnablePulse();

    static bool isButtonDetectEnabled();
    static void setButtonDetectEnabled(bool en);
    static bool isButtonDetectScanRequested();
    static void setButtonDetectScanRequested();
    static void clearButtonDetectScanRequested();

    enum LEDMode { LED_OFF, LED_ON, LED_BLINK };

    static void setRedMode(LEDMode mode);
    
    // 红灯告警开关
    static void setRedLedEnabled(bool enabled);
    static bool isRedLedEnabled();

private:
    static bool relay_master_state_;
    static bool relay_slave_state_;
    static bool locked_;
    static LEDMode red_mode_;
    static unsigned long blink_timer_;
    static bool blink_state_;
    
    // 定时器相关
    static bool timer_enabled_;
    static bool timer_running_;
    static uint16_t timer_duration_;
    static unsigned long timer_start_time_;
    static uint8_t timer_target_;   // 0=both relays, 1=master-only
    
    // 24小时循环功能相关
    static bool cycle_enabled_;
    static uint8_t cycle_start_h_, cycle_start_m_, cycle_end_h_, cycle_end_m_;
    static bool cycle_is_active_;
    static const uint8_t MAX_CYCLE_PERIODS = 3;
    struct CyclePeriod { uint8_t sh, sm, eh, em; };
    static CyclePeriod cycle_periods_[MAX_CYCLE_PERIODS];
    static uint8_t cycle_period_count_;

    static bool red_led_enabled_;    // 红灯告警是否启用
    static bool power_off_enabled_;
    static float power_off_threshold_;

    // 计费供电相关
    static bool billing_enabled_;
    static BillingMode billing_mode_;
    static float billing_threshold_;      // 目标用电量(kWh)
    static uint16_t billing_threshold_money_cents_; // 目标金额(分)
    static uint16_t billing_threshold_time_minutes_; // 目标时长(分钟)
    static float billing_start_energy_;   // 启动时的起始用电量(Wh)
    static uint32_t billing_start_time_;  // 启动时间(unix timestamp)
    static unsigned long billing_start_millis_; // 启动时的 millis()（无网络时备用）
    static BillingStopReason billing_stop_reason_;
    static float energy_price_;           // 电价(元/kWh)

    // WiFi 检测功能相关
    static bool wifi_detect_enabled_;
    static char wifi_detect_target_[33];  // 目标 SSID
    static char wifi_detect_mac_[18];     // 目标 MAC 地址
    static uint16_t wifi_detect_timeout_; // 消失后等待秒数
    static bool wifi_detect_link_timer_;  // 是否联锁倒计时
    static bool wifi_was_present_;        // 上次检测状态
    static int8_t wifi_detect_rssi_threshold_; // RSSI 信号阈值（如 -70）
    static uint8_t wifi_detect_scan_speed_;    // 扫描速度 0=slow,1=medium,2=fast
    static bool wifi_detect_power_checking_;   // 是否正在1分钟功率检测中
    static uint8_t wifi_detect_current_hour_;     // 当前小时（用于 WiFi 检测统计）
    static uint8_t wifi_detect_current_interval_; // 当前自动扫描间隔秒数
    static bool wifi_detect_only_master_;      // 人来上电仅打开主继电器
    static uint8_t timer_source_;              // 0=manual, 1=wifi_detect
    static bool eeprom_dirty_;
    static unsigned long enable_pulse_start_;
    static bool enable_pulse_active_;
};

#endif
