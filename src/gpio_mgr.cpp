#include "gpio_mgr.h"
#include "config.h"
#include <climits>
#include <ESP8266WiFi.h>
#include "sy7t609.h"
#include "mqtt_mgr.h"
#include "energy_mgr.h"

bool GPIOManager::relay_master_state_ = false;
bool GPIOManager::relay_slave_state_ = false;
bool GPIOManager::locked_ = false;
GPIOManager::LEDMode GPIOManager::blue_mode_ = LED_OFF;
GPIOManager::LEDMode GPIOManager::red_mode_ = LED_OFF;
unsigned long GPIOManager::blink_timer_ = 0;
bool GPIOManager::blink_state_ = false;
bool GPIOManager::red_led_enabled_ = true;   // 默认红灯开启

// 定时器相关
bool GPIOManager::timer_enabled_ = false;
bool GPIOManager::timer_running_ = false;
uint8_t GPIOManager::timer_duration_ = 0;
unsigned long GPIOManager::timer_start_time_ = 0;

#define EEPROM_LOCK_ADDR          200
#define EEPROM_MAGIC_LOCK         0x42
#define EEPROM_TIMER_ENABLED_ADDR 201
#define EEPROM_TIMER_DURATION_ADDR 202
#define EEPROM_RED_LED_ADDR       203
#define EEPROM_CYCLE_MAGIC_ADDR   204
#define EEPROM_CYCLE_ENABLED_ADDR 205
#define EEPROM_CYCLE_SH_ADDR      206
#define EEPROM_CYCLE_SM_ADDR      207
#define EEPROM_CYCLE_EH_ADDR      208
#define EEPROM_CYCLE_EM_ADDR      209
#define EEPROM_CYCLE_MAGIC        0xC8
#define EEPROM_CYCLE_MAGIC_V2     0xC9
#define EEPROM_CYCLE_COUNT_ADDR   206
#define EEPROM_CYCLE_PERIODS_ADDR 492

static inline unsigned long millisDiff(unsigned long now, unsigned long start) {
    return (now >= start) ? (now - start) : (ULONG_MAX - start + now + 1);
}

bool GPIOManager::cycle_enabled_ = false;
uint8_t GPIOManager::cycle_start_h_ = 0;
uint8_t GPIOManager::cycle_start_m_ = 0;
uint8_t GPIOManager::cycle_end_h_ = 0;
uint8_t GPIOManager::cycle_end_m_ = 0;
bool GPIOManager::cycle_is_active_ = false;
GPIOManager::CyclePeriod GPIOManager::cycle_periods_[MAX_CYCLE_PERIODS] = {};
uint8_t GPIOManager::cycle_period_count_ = 0;

bool GPIOManager::power_off_enabled_ = false;
float GPIOManager::power_off_threshold_ = 0.5f;

// 计费供电相关
bool GPIOManager::billing_enabled_ = false;
float GPIOManager::billing_threshold_ = 10.0f;
float GPIOManager::billing_start_energy_ = 0.0f;
float GPIOManager::energy_price_ = 0.6f;

// WiFi 检测功能相关
bool GPIOManager::wifi_detect_enabled_ = false;
char GPIOManager::wifi_detect_target_[33] = {0};
char GPIOManager::wifi_detect_mac_[18] = {0};
uint16_t GPIOManager::wifi_detect_timeout_ = 60;
bool GPIOManager::wifi_detect_link_timer_ = false;
unsigned long GPIOManager::wifi_last_seen_ = 0;
bool GPIOManager::wifi_was_present_ = false;
int8_t GPIOManager::wifi_detect_rssi_threshold_ = -70;
uint8_t GPIOManager::wifi_detect_scan_speed_ = 1;
bool GPIOManager::wifi_detect_power_checking_ = false;
unsigned long GPIOManager::wifi_detect_power_check_start_ = 0;
bool GPIOManager::wifi_detect_power_confirmed_ = false;
bool GPIOManager::wifi_detect_only_master_ = false;
uint8_t GPIOManager::timer_source_ = 0;
bool GPIOManager::eeprom_dirty_ = false;
unsigned long GPIOManager::enable_pulse_start_ = 0;
bool GPIOManager::enable_pulse_active_ = false;
static unsigned long eeprom_last_commit_ = 0;
static bool button_detect_scan_requested_ = false;
static uint16_t wifi_detect_hourly_appear_[24] = {0};
static uint16_t wifi_detect_hourly_total_[24] = {0};
static uint8_t wifi_detect_current_hour_ = 0;
static uint8_t wifi_detect_current_interval_ = 15;

#define EEPROM_POWER_OFF_ENABLED_ADDR 212
#define EEPROM_POWER_OFF_THRESHOLD_ADDR 213
#define EEPROM_BILLING_ENABLED_ADDR   214
#define EEPROM_BILLING_THRESHOLD_ADDR 215  // 2 bytes for kwh * 100
#define EEPROM_CONFIG_MAGIC_ADDR 199
#define EEPROM_CONFIG_MAGIC 0xD7

void GPIOManager::init() {
    initRelayPins();
    initLEDPins();
    initButtonPin();

    if (EEPROM.read(EEPROM_CONFIG_MAGIC_ADDR) != EEPROM_CONFIG_MAGIC) {
        EEPROM.write(EEPROM_POWER_OFF_ENABLED_ADDR, 0);
        EEPROM.write(EEPROM_POWER_OFF_THRESHOLD_ADDR, 50);
        EEPROM.write(EEPROM_BILLING_ENABLED_ADDR, 0);
        uint16_t bt = 1000;
        EEPROM.write(EEPROM_BILLING_THRESHOLD_ADDR, bt >> 8);
        EEPROM.write(EEPROM_BILLING_THRESHOLD_ADDR + 1, bt & 0xFF);
        EEPROM.write(218, 0);
        EEPROM.write(256, 0);
        EEPROM.write(EEPROM_CARD_VISIBILITY_ADDR, 0x1F);
        EEPROM.write(EEPROM_CONFIG_MAGIC_ADDR, EEPROM_CONFIG_MAGIC);
        EEPROM.commit();
        DBG_PRINTF("[GPIO] EEPROM config initialized with defaults\n");
    } else {
        uint8_t v256 = EEPROM.read(256);
        if (v256 != 0 && v256 != 1) {
            EEPROM.write(256, 0);
            EEPROM.commit();
            DBG_PRINTF("[GPIO] EEPROM addr 256 was invalid (%d), reset to 0\n", v256);
        }
    }

    if (EEPROM.read(EEPROM_LOCK_ADDR) == EEPROM_MAGIC_LOCK) {
        locked_ = true;
        DBG_PRINTF("[GPIO] Lock mode enabled\n");
    }
    
    // 加载定时器设置
    timer_enabled_ = (EEPROM.read(EEPROM_TIMER_ENABLED_ADDR) == 1);
    timer_duration_ = EEPROM.read(EEPROM_TIMER_DURATION_ADDR);
    if (timer_duration_ > 24) timer_duration_ = 1;
    
    // 加载红灯设置（默认开启，0x00=明确关闭，其他=开启）
    uint8_t redLedVal = EEPROM.read(EEPROM_RED_LED_ADDR);
    red_led_enabled_ = (redLedVal != 0x00);  // 未初始化(0xFF)或明确开启都默认开启

    if (EEPROM.read(EEPROM_CYCLE_MAGIC_ADDR) == EEPROM_CYCLE_MAGIC_V2) {
        cycle_enabled_ = (EEPROM.read(EEPROM_CYCLE_ENABLED_ADDR) == 1);
        cycle_period_count_ = EEPROM.read(EEPROM_CYCLE_COUNT_ADDR);
        if (cycle_period_count_ > MAX_CYCLE_PERIODS) cycle_period_count_ = 0;
        for (uint8_t i = 0; i < cycle_period_count_; i++) {
            int addr = EEPROM_CYCLE_PERIODS_ADDR + i * 4;
            cycle_periods_[i].sh = EEPROM.read(addr);
            cycle_periods_[i].sm = EEPROM.read(addr + 1);
            cycle_periods_[i].eh = EEPROM.read(addr + 2);
            cycle_periods_[i].em = EEPROM.read(addr + 3);
        }
    } else if (EEPROM.read(EEPROM_CYCLE_MAGIC_ADDR) == EEPROM_CYCLE_MAGIC) {
        cycle_enabled_ = (EEPROM.read(EEPROM_CYCLE_ENABLED_ADDR) == 1);
        uint8_t old_sh = EEPROM.read(EEPROM_CYCLE_SH_ADDR);
        uint8_t old_sm = EEPROM.read(EEPROM_CYCLE_SM_ADDR);
        uint8_t old_eh = EEPROM.read(EEPROM_CYCLE_EH_ADDR);
        uint8_t old_em = EEPROM.read(EEPROM_CYCLE_EM_ADDR);
        if (old_sh <= 23 && old_sm <= 59 && old_eh <= 23 && old_em <= 59) {
            cycle_periods_[0] = {old_sh, old_sm, old_eh, old_em};
            cycle_period_count_ = 1;
        } else {
            cycle_period_count_ = 0;
        }
        EEPROM.write(EEPROM_CYCLE_MAGIC_ADDR, EEPROM_CYCLE_MAGIC_V2);
        EEPROM.write(EEPROM_CYCLE_COUNT_ADDR, cycle_period_count_);
        for (uint8_t i = 0; i < cycle_period_count_; i++) {
            int addr = EEPROM_CYCLE_PERIODS_ADDR + i * 4;
            EEPROM.write(addr, cycle_periods_[i].sh);
            EEPROM.write(addr + 1, cycle_periods_[i].sm);
            EEPROM.write(addr + 2, cycle_periods_[i].eh);
            EEPROM.write(addr + 3, cycle_periods_[i].em);
        }
        EEPROM.commit();
        DBG_PRINTF("[GPIO] Cycle data migrated to V2 format\n");
    } else {
        cycle_enabled_ = false;
        cycle_period_count_ = 0;
    }
    cycle_start_h_ = cycle_period_count_ > 0 ? cycle_periods_[0].sh : 0;
    cycle_start_m_ = cycle_period_count_ > 0 ? cycle_periods_[0].sm : 0;
    cycle_end_h_ = cycle_period_count_ > 0 ? cycle_periods_[0].eh : 0;
    cycle_end_m_ = cycle_period_count_ > 0 ? cycle_periods_[0].em : 0;

    // 加载 WiFi 检测功能设置（默认关闭）
    uint8_t wdEn = EEPROM.read(218);
    wifi_detect_enabled_ = (wdEn == 1);
    wifi_detect_rssi_threshold_ = (int8_t)EEPROM.read(288);
    if (wifi_detect_rssi_threshold_ == 0 || wifi_detect_rssi_threshold_ > -40) {
        wifi_detect_rssi_threshold_ = -70;  // 默认 -70dBm
    }
    for (int i = 0; i < 33; i++) {
        wifi_detect_target_[i] = EEPROM.read(219 + i);
    }
    wifi_detect_target_[32] = 0;
    if (wifi_detect_target_[0] == 0 || wifi_detect_target_[0] == 0xFF || wifi_detect_target_[0] < 32) {
        memset(wifi_detect_target_, 0, sizeof(wifi_detect_target_));
    }
    wifi_detect_link_timer_ = (EEPROM.read(255) == 1);
    wifi_detect_scan_speed_ = EEPROM.read(289);
    if (wifi_detect_scan_speed_ > 3) wifi_detect_scan_speed_ = 1;
    wifi_detect_only_master_ = (EEPROM.read(256) == 1);

    uint16_t timeout_raw = (EEPROM.read(253) << 8) | EEPROM.read(252);
    if (timeout_raw >= 10 && timeout_raw <= 3600) {
        wifi_detect_timeout_ = timeout_raw;
    }

    for (int h = 0; h < 24; h++) {
        int addr = 396 + h * 4;
        uint16_t a = (EEPROM.read(addr) << 8) | EEPROM.read(addr + 1);
        uint16_t b = (EEPROM.read(addr + 2) << 8) | EEPROM.read(addr + 3);
        if (a <= 3000 && b <= 3000) {
            wifi_detect_hourly_appear_[h] = a;
            wifi_detect_hourly_total_[h] = b;
        }
    }

    DBG_PRINTF("[GPIO] WiFi detect enabled: %s, target: %s, rssi: %d\n", 
        wifi_detect_enabled_ ? "yes" : "no", wifi_detect_target_, wifi_detect_rssi_threshold_);
    if(cycle_start_h_ > 23) cycle_start_h_ = 0;
    if(cycle_start_m_ > 59) cycle_start_m_ = 0;
    if(cycle_end_h_ > 23) cycle_end_h_ = 0;
    if(cycle_end_m_ > 59) cycle_end_m_ = 0;

    // 加载设备拔除断电设置
    power_off_enabled_ = (EEPROM.read(EEPROM_POWER_OFF_ENABLED_ADDR) == 1);
    uint8_t thresh_raw = EEPROM.read(EEPROM_POWER_OFF_THRESHOLD_ADDR);
    if (thresh_raw > 0 && thresh_raw <= 200) {
        power_off_threshold_ = thresh_raw / 100.0f;
    } else {
        // EEPROM 数据无效，使用默认值并写入
        power_off_threshold_ = 0.5f;
        EEPROM.write(EEPROM_POWER_OFF_THRESHOLD_ADDR, (uint8_t)(power_off_threshold_ * 100));
        EEPROM.commit();
        DBG_PRINTF("[GPIO] Power off threshold EEPROM invalid, using default: %.2f W\n", power_off_threshold_);
    }

    // 加载计费供电设置
    billing_enabled_ = (EEPROM.read(EEPROM_BILLING_ENABLED_ADDR) == 1);
    uint16_t billing_raw = (EEPROM.read(EEPROM_BILLING_THRESHOLD_ADDR) << 8) | EEPROM.read(EEPROM_BILLING_THRESHOLD_ADDR + 1);
    if (billing_raw > 0 && billing_raw <= 5000) {
        billing_threshold_ = billing_raw / 100.0f;
    } else {
        // EEPROM 数据无效，使用默认值并写入
        billing_threshold_ = 10.0f;
        EEPROM.write(EEPROM_BILLING_THRESHOLD_ADDR, (uint16_t)(billing_threshold_ * 100) >> 8);
        EEPROM.write(EEPROM_BILLING_THRESHOLD_ADDR + 1, (uint16_t)(billing_threshold_ * 100) & 0xFF);
        EEPROM.commit();
        DBG_PRINTF("[GPIO] Billing threshold EEPROM invalid, using default: %.2f kWh\n", billing_threshold_);
    }

    DBG_PRINTF("[GPIO] Timer: %s, Duration: %dh\n", timer_enabled_ ? "enabled" : "disabled", timer_duration_);

    uint16_t price_raw = (EEPROM.read(290) << 8) | EEPROM.read(291);
    if (price_raw > 0 && price_raw <= 2000) {
        energy_price_ = price_raw / 100.0f;
    } else {
        energy_price_ = 0.6f;
    }

    if (billing_enabled_) {
        uint8_t bse_buf[4] = {0};
        for (int i = 0; i < 4; i++) bse_buf[i] = EEPROM.read(292 + i);
        memcpy(&billing_start_energy_, bse_buf, sizeof(float));
        if (billing_start_energy_ < 0 || billing_start_energy_ > 999999) {
            billing_start_energy_ = 0;
        }
    }
}

void GPIOManager::initRelayPins() {
    pinMode(RELAY_MASTER_PIN, OUTPUT);
    pinMode(RELAY_SLAVE_PIN, OUTPUT);
    pinMode(RELAY_ENABLE_PIN, OUTPUT);

    digitalWrite(RELAY_MASTER_PIN, HIGH);
    digitalWrite(RELAY_SLAVE_PIN, HIGH);
    digitalWrite(RELAY_ENABLE_PIN, HIGH);

    relay_master_state_ = false;
    relay_slave_state_ = false;
    triggerRelayEnable();

    DBG_PRINTF("[GPIO] Relay pins initialized (active LOW)\n");
}

void GPIOManager::initLEDPins() {
    pinMode(LED_BLUE_PIN, OUTPUT);
    pinMode(LED_RED_PIN, OUTPUT);
    pinMode(LED_WHITE_PIN, OUTPUT);

    digitalWrite(LED_BLUE_PIN, HIGH);
    digitalWrite(LED_RED_PIN, HIGH);
    digitalWrite(LED_WHITE_PIN, HIGH);
}

void GPIOManager::initButtonPin() {
    pinMode(BUTTON_PIN, INPUT_PULLUP);
}

void GPIOManager::setRelayMaster(bool on) {
    if (on && timer_running_) {
        stopTimer();
    }
    relay_master_state_ = on;
    digitalWrite(RELAY_MASTER_PIN, on ? LOW : HIGH);
    if (on) {
        triggerRelayEnable();
        setLEDWhite(relay_slave_state_);
    } else {
        relay_slave_state_ = false;
        digitalWrite(RELAY_SLAVE_PIN, HIGH);
        triggerRelayEnable();
        setLEDWhite(false);
    }
    DBG_PRINTF("[GPIO] Master relay: %s\n", on ? "ON" : "OFF");
    MQTTManager::requestPublish();
}

void GPIOManager::setRelaySlave(bool on) {
    if (on && !relay_master_state_) {
        DBG_PRINTF("[GPIO] Auto-enabling master relay for slave\n");
        setRelayMaster(true);
    }
    relay_slave_state_ = on;
    digitalWrite(RELAY_SLAVE_PIN, on ? LOW : HIGH);
    triggerRelayEnable();
    setLEDWhite(on && relay_master_state_);
    DBG_PRINTF("[GPIO] Slave relay: %s\n", on ? "ON" : "OFF");
    MQTTManager::requestPublish();
}

void GPIOManager::setRelays(bool master, bool slave) {
    if ((master || slave) && timer_running_) {
        stopTimer();
    }
    relay_master_state_ = master;
    relay_slave_state_ = slave;
    digitalWrite(RELAY_MASTER_PIN, master ? LOW : HIGH);
    digitalWrite(RELAY_SLAVE_PIN, slave ? LOW : HIGH);
    triggerRelayEnable();
    setLEDWhite(slave && master);
    DBG_PRINTF("[GPIO] Relays: Master=%s Slave=%s\n", master ? "ON" : "OFF", slave ? "ON" : "OFF");
    MQTTManager::requestPublish();
}

void GPIOManager::setLocked(bool locked) {
    locked_ = locked;
    EEPROM.write(EEPROM_LOCK_ADDR, locked ? EEPROM_MAGIC_LOCK : 0);
    requestEEPROMCommit();

    DBG_PRINTF("[GPIO] Lock: %s\n", locked ? "enabled" : "disabled");
}

bool GPIOManager::isLocked() {
    return locked_;
}

void GPIOManager::triggerRelayEnable() {
    digitalWrite(RELAY_ENABLE_PIN, LOW);
    enable_pulse_start_ = millis();
    enable_pulse_active_ = true;
}

void GPIOManager::handleEnablePulse() {
    if (enable_pulse_active_ && millis() - enable_pulse_start_ >= RELAY_ENABLE_PULSE_MS) {
        digitalWrite(RELAY_ENABLE_PIN, HIGH);
        enable_pulse_active_ = false;
    }
}

bool GPIOManager::getRelayMaster() {
    return relay_master_state_;
}

bool GPIOManager::getRelaySlave() {
    return relay_slave_state_;
}

void GPIOManager::setLEDBlue(bool on, bool blink) {
    digitalWrite(LED_BLUE_PIN, on ? LOW : HIGH);
    if (blink) {
        blue_mode_ = LED_BLINK;
    } else {
        blue_mode_ = on ? LED_ON : LED_OFF;
    }
}

void GPIOManager::setLEDRed(bool on, bool blink) {
    digitalWrite(LED_RED_PIN, on ? LOW : HIGH);
    if (blink) {
        red_mode_ = LED_BLINK;
    } else {
        red_mode_ = on ? LED_ON : LED_OFF;
    }
}

void GPIOManager::setLEDWhite(bool on) {
    digitalWrite(LED_WHITE_PIN, on ? LOW : HIGH);
}

void GPIOManager::setBlueMode(LEDMode mode) {
    // WiFi 状态不再控制蓝灯，保留该函数为空或直接 return
}

void GPIOManager::setRedMode(LEDMode mode) {
    if (red_led_enabled_) {
        red_mode_ = mode;
    } else {
        red_mode_ = LED_OFF;  // 红灯已禁用，始终熔灯
    }
}

void GPIOManager::setRedLedEnabled(bool enabled) {
    red_led_enabled_ = enabled;
    EEPROM.write(EEPROM_RED_LED_ADDR, enabled ? 0x01 : 0x00);
    requestEEPROMCommit();
    if (!enabled) {
        red_mode_ = LED_OFF;
        digitalWrite(LED_RED_PIN, HIGH);
    }
    DBG_PRINTF("[GPIO] Red LED: %s\n", enabled ? "enabled" : "disabled");
}

bool GPIOManager::isRedLedEnabled() {
    return red_led_enabled_;
}

void GPIOManager::updateLEDs() {
    if (timer_running_ && timer_enabled_) {
        if (millisDiff(millis(), blink_timer_) > 500) {
            blink_timer_ = millis();
            blink_state_ = !blink_state_;
            digitalWrite(LED_WHITE_PIN, blink_state_ ? LOW : HIGH);
        }
    }

    if (cycle_enabled_) {
        digitalWrite(LED_BLUE_PIN, LOW);
    } else {
        digitalWrite(LED_BLUE_PIN, HIGH);
    }

    if (WiFi.status() != WL_CONNECTED && red_led_enabled_) {
        if (millisDiff(millis(), blink_timer_) > 500) {
            blink_timer_ = millis();
            blink_state_ = !blink_state_;
            digitalWrite(LED_RED_PIN, blink_state_ ? LOW : HIGH);
        }
    } else if (red_mode_ == LED_ON && red_led_enabled_) {
        digitalWrite(LED_RED_PIN, LOW);
    } else {
        digitalWrite(LED_RED_PIN, HIGH);
    }
}

// 定时器功能实现
void GPIOManager::setTimerEnabled(bool enabled) {
    timer_enabled_ = enabled;
    if (!enabled) {
        stopTimer();
    }
    EEPROM.write(EEPROM_TIMER_ENABLED_ADDR, enabled ? 1 : 0);
    requestEEPROMCommit();
    DBG_PRINTF("[GPIO] Timer enabled: %s\n", enabled ? "yes" : "no");
}

bool GPIOManager::isTimerEnabled() {
    return timer_enabled_;
}

void GPIOManager::setTimerDuration(uint8_t hours) {
    if (hours < 1 || hours > 24) {
        hours = 1;
    }
    timer_duration_ = hours;
    EEPROM.write(EEPROM_TIMER_DURATION_ADDR, hours);
    requestEEPROMCommit();
    DBG_PRINTF("[GPIO] Timer duration set to: %dh\n", hours);
}

uint8_t GPIOManager::getTimerDuration() {
    return timer_duration_;
}

void GPIOManager::startTimer() {
    if (timer_enabled_ && !timer_running_) {
        timer_running_ = true;
        timer_start_time_ = millis();
        relay_master_state_ = true;
        relay_slave_state_ = true;
        digitalWrite(RELAY_MASTER_PIN, LOW);
        digitalWrite(RELAY_SLAVE_PIN, LOW);
        triggerRelayEnable();
        setLEDWhite(true);
        DBG_PRINTF("[GPIO] Timer started: %dh\n", timer_duration_);
    }
}

void GPIOManager::stopTimer() {
    if (timer_running_) {
        timer_running_ = false;
        timer_start_time_ = 0;
        relay_master_state_ = false;
        relay_slave_state_ = false;
        digitalWrite(RELAY_MASTER_PIN, HIGH);
        digitalWrite(RELAY_SLAVE_PIN, HIGH);
        triggerRelayEnable();
        setLEDWhite(false);
        DBG_PRINTF("[GPIO] Timer stopped\n");
    }
}

bool GPIOManager::isTimerRunning() {
    return timer_running_;
}

unsigned long GPIOManager::getTimerRemaining() {
    if (!timer_running_) {
        return 0;
    }
    unsigned long elapsed = millisDiff(millis(), timer_start_time_) / 1000;
    unsigned long total = timer_duration_ * 3600UL;
    if (elapsed >= total) {
        return 0;
    }
    return total - elapsed;
}

void GPIOManager::handleTimer() {
    if (timer_running_ && timer_enabled_) {
        unsigned long elapsed = millisDiff(millis(), timer_start_time_) / 1000;
        unsigned long total = timer_duration_ * 3600UL;

        if (elapsed >= total) {
            DBG_PRINTF("[GPIO] Timer expired\n");
            stopTimer();
        }
    }

    // 24小时间段循环开启任务 (需要 NTP 完成同步后有效，与定时器互斥)
    if (cycle_enabled_ && !timer_running_ && cycle_period_count_ > 0) {
        time_t now = time(nullptr);
        struct tm* timeinfo = localtime(&now);

        if (timeinfo && timeinfo->tm_year > 100) {
            int current_mins = timeinfo->tm_hour * 60 + timeinfo->tm_min;

            bool should_be_on = false;
            for (uint8_t i = 0; i < cycle_period_count_; i++) {
                int start_mins = cycle_periods_[i].sh * 60 + cycle_periods_[i].sm;
                int end_mins = cycle_periods_[i].eh * 60 + cycle_periods_[i].em;

                if (start_mins <= end_mins) {
                    if (current_mins >= start_mins && current_mins < end_mins) {
                        should_be_on = true;
                        break;
                    }
                } else {
                    if (current_mins >= start_mins || current_mins < end_mins) {
                        should_be_on = true;
                        break;
                    }
                }
            }

            if (should_be_on && !cycle_is_active_) {
                cycle_is_active_ = true;
                DBG_PRINTF("[GPIO] Cycle ON timeframe reached\n");
                setRelays(true, !wifi_detect_only_master_);
            } else if (!should_be_on && cycle_is_active_) {
                cycle_is_active_ = false;
                DBG_PRINTF("[GPIO] Cycle OFF timeframe reached\n");
                setRelayMaster(false);
            }
        }
    }
}

// 24小时循环控制实现
void GPIOManager::setCycleEnabled(bool enabled) {
    cycle_enabled_ = enabled;
    EEPROM.write(EEPROM_CYCLE_MAGIC_ADDR, EEPROM_CYCLE_MAGIC_V2);
    EEPROM.write(EEPROM_CYCLE_ENABLED_ADDR, enabled ? 1 : 0);
    requestEEPROMCommit();
    DBG_PRINTF("[GPIO] Cycle enabled: %s\n", enabled ? "yes" : "no");
}

bool GPIOManager::isCycleEnabled() {
    return cycle_enabled_;
}

void GPIOManager::clearCycleActive() {
    cycle_is_active_ = false;
}

void GPIOManager::setCycleTime(uint8_t start_h, uint8_t start_m, uint8_t end_h, uint8_t end_m) {
    cycle_start_h_ = start_h;
    cycle_start_m_ = start_m;
    cycle_end_h_ = end_h;
    cycle_end_m_ = end_m;
    if (cycle_period_count_ == 0) {
        cycle_periods_[0] = {start_h, start_m, end_h, end_m};
        cycle_period_count_ = 1;
        EEPROM.write(EEPROM_CYCLE_COUNT_ADDR, cycle_period_count_);
    } else {
        cycle_periods_[0].sh = start_h;
        cycle_periods_[0].sm = start_m;
        cycle_periods_[0].eh = end_h;
        cycle_periods_[0].em = end_m;
    }
    EEPROM.write(EEPROM_CYCLE_MAGIC_ADDR, EEPROM_CYCLE_MAGIC_V2);
    int addr = EEPROM_CYCLE_PERIODS_ADDR;
    EEPROM.write(addr, cycle_periods_[0].sh);
    EEPROM.write(addr + 1, cycle_periods_[0].sm);
    EEPROM.write(addr + 2, cycle_periods_[0].eh);
    EEPROM.write(addr + 3, cycle_periods_[0].em);
    requestEEPROMCommit();
    DBG_PRINTF("[GPIO] Cycle time set: %02d:%02d to %02d:%02d\n", start_h, start_m, end_h, end_m);
}

void GPIOManager::getCycleTime(uint8_t& start_h, uint8_t& start_m, uint8_t& end_h, uint8_t& end_m) {
    start_h = cycle_start_h_;
    start_m = cycle_start_m_;
    end_h = cycle_end_h_;
    end_m = cycle_end_m_;
}

void GPIOManager::setCyclePeriod(uint8_t idx, uint8_t sh, uint8_t sm, uint8_t eh, uint8_t em) {
    if (idx >= MAX_CYCLE_PERIODS) return;
    if (idx > cycle_period_count_) return;
    cycle_periods_[idx] = {sh, sm, eh, em};
    if (idx == cycle_period_count_) {
        cycle_period_count_++;
        EEPROM.write(EEPROM_CYCLE_COUNT_ADDR, cycle_period_count_);
    }
    int addr = EEPROM_CYCLE_PERIODS_ADDR + idx * 4;
    EEPROM.write(addr, sh);
    EEPROM.write(addr + 1, sm);
    EEPROM.write(addr + 2, eh);
    EEPROM.write(addr + 3, em);
    EEPROM.write(EEPROM_CYCLE_MAGIC_ADDR, EEPROM_CYCLE_MAGIC_V2);
    requestEEPROMCommit();
    if (idx == 0) {
        cycle_start_h_ = sh; cycle_start_m_ = sm;
        cycle_end_h_ = eh; cycle_end_m_ = em;
    }
    DBG_PRINTF("[GPIO] Cycle period %d set: %02d:%02d-%02d:%02d\n", idx, sh, sm, eh, em);
}

bool GPIOManager::getCyclePeriod(uint8_t idx, uint8_t& sh, uint8_t& sm, uint8_t& eh, uint8_t& em) {
    if (idx >= cycle_period_count_) return false;
    sh = cycle_periods_[idx].sh;
    sm = cycle_periods_[idx].sm;
    eh = cycle_periods_[idx].eh;
    em = cycle_periods_[idx].em;
    return true;
}

uint8_t GPIOManager::getCyclePeriodCount() {
    return cycle_period_count_;
}

bool GPIOManager::addCyclePeriod(uint8_t sh, uint8_t sm, uint8_t eh, uint8_t em) {
    if (cycle_period_count_ >= MAX_CYCLE_PERIODS) return false;
    cycle_periods_[cycle_period_count_] = {sh, sm, eh, em};
    int addr = EEPROM_CYCLE_PERIODS_ADDR + cycle_period_count_ * 4;
    EEPROM.write(addr, sh);
    EEPROM.write(addr + 1, sm);
    EEPROM.write(addr + 2, eh);
    EEPROM.write(addr + 3, em);
    cycle_period_count_++;
    EEPROM.write(EEPROM_CYCLE_COUNT_ADDR, cycle_period_count_);
    EEPROM.write(EEPROM_CYCLE_MAGIC_ADDR, EEPROM_CYCLE_MAGIC_V2);
    requestEEPROMCommit();
    DBG_PRINTF("[GPIO] Cycle period added (%d): %02d:%02d-%02d:%02d\n", cycle_period_count_, sh, sm, eh, em);
    return true;
}

bool GPIOManager::removeCyclePeriod(uint8_t idx) {
    if (idx >= cycle_period_count_) return false;
    for (uint8_t i = idx; i < cycle_period_count_ - 1; i++) {
        cycle_periods_[i] = cycle_periods_[i + 1];
    }
    cycle_period_count_--;
    cycle_periods_[cycle_period_count_] = {0, 0, 0, 0};
    EEPROM.write(EEPROM_CYCLE_COUNT_ADDR, cycle_period_count_);
    for (uint8_t i = 0; i < MAX_CYCLE_PERIODS; i++) {
        int addr = EEPROM_CYCLE_PERIODS_ADDR + i * 4;
        EEPROM.write(addr, cycle_periods_[i].sh);
        EEPROM.write(addr + 1, cycle_periods_[i].sm);
        EEPROM.write(addr + 2, cycle_periods_[i].eh);
        EEPROM.write(addr + 3, cycle_periods_[i].em);
    }
    EEPROM.write(EEPROM_CYCLE_MAGIC_ADDR, EEPROM_CYCLE_MAGIC_V2);
    requestEEPROMCommit();
    if (cycle_period_count_ > 0) {
        cycle_start_h_ = cycle_periods_[0].sh; cycle_start_m_ = cycle_periods_[0].sm;
        cycle_end_h_ = cycle_periods_[0].eh; cycle_end_m_ = cycle_periods_[0].em;
    }
    DBG_PRINTF("[GPIO] Cycle period %d removed, count=%d\n", idx, cycle_period_count_);
    return true;
}

void GPIOManager::setPowerOffEnabled(bool enabled) {
    power_off_enabled_ = enabled;
    if (enabled && !SY7T609::isEnabled()) {
        SY7T609::setEnabled(true);
    }
    EEPROM.write(EEPROM_POWER_OFF_ENABLED_ADDR, enabled ? 1 : 0);
    requestEEPROMCommit();
    DBG_PRINTF("[GPIO] Power off enabled: %s\n", enabled ? "yes" : "no");
}

bool GPIOManager::isPowerOffEnabled() {
    return power_off_enabled_;
}

void GPIOManager::setPowerOffThreshold(float threshold) {
    if (threshold < 0.1f) threshold = 0.1f;
    if (threshold > 5.0f) threshold = 5.0f;
    power_off_threshold_ = threshold;
    EEPROM.write(EEPROM_POWER_OFF_THRESHOLD_ADDR, (uint8_t)(threshold * 100));
    requestEEPROMCommit();
    DBG_PRINTF("[GPIO] Power off threshold: %.2f W\n", threshold);
}

float GPIOManager::getPowerOffThreshold() {
    return power_off_threshold_;
}

void GPIOManager::setBillingEnabled(bool enabled) {
    if (enabled && power_off_enabled_) {
        power_off_enabled_ = false;
        EEPROM.write(EEPROM_POWER_OFF_ENABLED_ADDR, 0);
    }
    billing_enabled_ = enabled;
    if (enabled) {
        billing_start_energy_ = EnergyManager::getTotalEnergy();
        uint8_t bse_buf[4] = {0};
        memcpy(bse_buf, &billing_start_energy_, sizeof(float));
        for (int i = 0; i < 4; i++) EEPROM.write(292 + i, bse_buf[i]);
    }
    EEPROM.write(EEPROM_BILLING_ENABLED_ADDR, enabled ? 1 : 0);
    requestEEPROMCommit();
    DBG_PRINTF("[GPIO] Billing enabled: %s, threshold: %.2f kWh\n", enabled ? "yes" : "no", billing_threshold_);
}

bool GPIOManager::isBillingEnabled() {
    return billing_enabled_;
}

void GPIOManager::setBillingThreshold(float kwh) {
    if (kwh < 1) kwh = 1;
    if (kwh > 50) kwh = 50;
    billing_threshold_ = kwh;
    uint16_t raw = (uint16_t)(kwh * 100);
    EEPROM.write(EEPROM_BILLING_THRESHOLD_ADDR, (raw >> 8) & 0xFF);
    EEPROM.write(EEPROM_BILLING_THRESHOLD_ADDR + 1, raw & 0xFF);
    requestEEPROMCommit();
    DBG_PRINTF("[GPIO] Billing threshold: %.2f kWh\n", kwh);
}

float GPIOManager::getBillingThreshold() {
    return billing_threshold_;
}

float GPIOManager::getBillingStartEnergy() {
    return billing_start_energy_;
}

void GPIOManager::setBillingStartEnergy(float energy) {
    billing_start_energy_ = energy;
    uint8_t bse_buf[4] = {0};
    memcpy(bse_buf, &billing_start_energy_, sizeof(float));
    for (int i = 0; i < 4; i++) EEPROM.write(292 + i, bse_buf[i]);
    requestEEPROMCommit();
}

void GPIOManager::setWiFiDetectEnabled(bool enabled) {
    wifi_detect_enabled_ = enabled;
    if (enabled) {
        wifi_was_present_ = false;
        wifi_last_seen_ = 0;
        wifi_detect_power_confirmed_ = false;
    } else {
        wifi_detect_power_checking_ = false;
        wifi_detect_power_confirmed_ = false;
        if (timer_source_ == 1 && timer_running_) {
            stopTimer();
            timer_source_ = 0;
            DBG_PRINTF("[GPIO] WiFi detect disabled, stopping wifi-sourced timer\n");
        }
    }
    EEPROM.write(218, enabled ? 1 : 0);
    DBG_PRINTF("[GPIO] WiFi detect: %s\n", enabled ? "enabled" : "disabled");
}

bool GPIOManager::isWiFiDetectEnabled() {
    return wifi_detect_enabled_;
}

void GPIOManager::setWiFiDetectTarget(const char* ssid) {
    if (ssid && strlen(ssid) > 0) {
        strncpy(wifi_detect_target_, ssid, 32);
        wifi_detect_target_[32] = 0;
        for (int i = 0; i < 33; i++) {
            EEPROM.write(219 + i, wifi_detect_target_[i]);
        }
        requestEEPROMCommit();
        DBG_PRINTF("[GPIO] WiFi detect target: %s\n", ssid);
    }
}

String GPIOManager::getWiFiDetectTarget() {
    return String(wifi_detect_target_);
}

void GPIOManager::setWiFiDetectTimeout(uint16_t seconds) {
    wifi_detect_timeout_ = seconds;
    EEPROM.write(252, seconds & 0xFF);
    EEPROM.write(253, (seconds >> 8) & 0xFF);
    requestEEPROMCommit();
}

uint16_t GPIOManager::getWiFiDetectTimeout() {
    return wifi_detect_timeout_;
}

void GPIOManager::setWiFiDetectLinkTimer(bool linked) {
    wifi_detect_link_timer_ = linked;
    EEPROM.write(255, linked ? 1 : 0);
    requestEEPROMCommit();
}

bool GPIOManager::isWiFiDetectLinkTimer() {
    return wifi_detect_link_timer_;
}

void GPIOManager::setWiFiDetectPresent(bool present) {
    wifi_was_present_ = present;
    wifi_last_seen_ = millis();
}

bool GPIOManager::isWiFiDetectPresent() {
    return wifi_was_present_;
}

void GPIOManager::setWiFiDetectRssiThreshold(int8_t rssi) {
    wifi_detect_rssi_threshold_ = rssi;
    EEPROM.write(288, (uint8_t)rssi);
    requestEEPROMCommit();
}

int8_t GPIOManager::getWiFiDetectRssiThreshold() {
    return wifi_detect_rssi_threshold_;
}

void GPIOManager::setWiFiDetectScanSpeed(uint8_t speed) {
    wifi_detect_scan_speed_ = (speed > 3) ? 3 : speed;
    EEPROM.write(289, wifi_detect_scan_speed_);
    requestEEPROMCommit();
}

uint8_t GPIOManager::getWiFiDetectScanSpeed() {
    return wifi_detect_scan_speed_;
}

unsigned long GPIOManager::getWiFiDetectScanInterval() {
    switch (wifi_detect_scan_speed_) {
        case 3: return getWiFiDetectAutoInterval();
        case 2: return 5000;
        case 1: return 15000;
        default: return 30000;
    }
}

void GPIOManager::setWiFiDetectMac(const char* mac) {
    strncpy(wifi_detect_mac_, mac, 17);
    wifi_detect_mac_[17] = '\0';
}

String GPIOManager::getWiFiDetectMac() {
    return String(wifi_detect_mac_);
}

void GPIOManager::setWiFiDetectPowerCheck(bool checking) {
    wifi_detect_power_checking_ = checking;
    if (checking) {
        wifi_detect_power_check_start_ = millis();
        wifi_detect_power_confirmed_ = false;
    }
}

bool GPIOManager::isWiFiDetectPowerChecking() {
    return wifi_detect_power_checking_;
}

unsigned long GPIOManager::getWiFiDetectPowerCheckStart() {
    return wifi_detect_power_check_start_;
}

void GPIOManager::setTimerSource(uint8_t source) {
    timer_source_ = source;
}

uint8_t GPIOManager::getTimerSource() {
    return timer_source_;
}

bool GPIOManager::isButtonAutoTimerEnabled() {
    return EEPROM.read(EEPROM_BUTTON_AUTO_TIMER_ADDR) != 0;
}

void GPIOManager::setButtonAutoTimerEnabled(bool en) {
    EEPROM.write(EEPROM_BUTTON_AUTO_TIMER_ADDR, en ? 1 : 0);
    EEPROM.commit();
}

bool GPIOManager::isButtonDetectEnabled() {
    return EEPROM.read(EEPROM_BUTTON_DETECT_ADDR) != 0;
}

void GPIOManager::setButtonDetectEnabled(bool en) {
    EEPROM.write(EEPROM_BUTTON_DETECT_ADDR, en ? 1 : 0);
    EEPROM.commit();
}

bool GPIOManager::isButtonDetectScanRequested() {
    return button_detect_scan_requested_;
}

void GPIOManager::setButtonDetectScanRequested() {
    button_detect_scan_requested_ = true;
}

void GPIOManager::clearButtonDetectScanRequested() {
    button_detect_scan_requested_ = false;
}

String GPIOManager::getMDNSHostname() {
    char buf[32] = {0};
    int len = 0;
    for (int i = 0; i < 31; i++) {
        char c = EEPROM.read(EEPROM_MDNS_HOSTNAME_ADDR + i);
        if (c >= 32 && c <= 126) {
            buf[len++] = c;
        } else {
            break;
        }
    }
    buf[len] = '\0';
    if (len == 0) return "power";
    return String(buf);
}

void GPIOManager::setMDNSHostname(const String& name) {
    int len = name.length();
    if (len > 31) len = 31;
    for (int i = 0; i < 31; i++) {
        EEPROM.write(EEPROM_MDNS_HOSTNAME_ADDR + i, i < len ? name[i] : 0);
    }
    if (len > 0) {
        EEPROM.commit();
    }
}

void GPIOManager::setWiFiDetectRelayOnlyMaster(bool only) {
    wifi_detect_only_master_ = only;
    EEPROM.write(256, only ? 1 : 0);
    requestEEPROMCommit();
    DBG_PRINTF("[GPIO] WiFi detect only master: %s\n", only ? "yes" : "no");
}

bool GPIOManager::getWiFiDetectRelayOnlyMaster() {
    return wifi_detect_only_master_;
}

float GPIOManager::getEnergyPrice() {
    return energy_price_;
}

void GPIOManager::setEnergyPrice(float price) {
    if (price < 0.1f) price = 0.1f;
    if (price > 10.0f) price = 10.0f;
    energy_price_ = price;
    uint16_t raw = (uint16_t)(price * 100);
    EEPROM.write(290, (raw >> 8) & 0xFF);
    EEPROM.write(291, raw & 0xFF);
    requestEEPROMCommit();
}

void GPIOManager::setWiFiDetectPowerConfirmed(bool confirmed) {
    wifi_detect_power_confirmed_ = confirmed;
}

bool GPIOManager::isWiFiDetectPowerConfirmed() {
    return wifi_detect_power_confirmed_;
}

void GPIOManager::recordWiFiDetectHourly(bool found) {
    time_t now = time(nullptr);
    struct tm* timeinfo = localtime(&now);
    if (!timeinfo || timeinfo->tm_year < 100) return;
    uint8_t h = timeinfo->tm_hour;
    wifi_detect_current_hour_ = h;
    wifi_detect_hourly_total_[h]++;
    if (found) {
        wifi_detect_hourly_appear_[h]++;
    }
    uint8_t old_interval = wifi_detect_current_interval_;
    unsigned long new_interval = GPIOManager::getWiFiDetectAutoInterval();
    wifi_detect_current_interval_ = new_interval / 1000;
    if (old_interval != wifi_detect_current_interval_) {
        DBG_PRINTF("[WiFiDetect] Auto interval changed: %ds -> %ds\n", old_interval, wifi_detect_current_interval_);
    }
    if (wifi_detect_hourly_total_[h] % 10 == 0) {
        for (int i = 0; i < 24; i++) {
            int addr = 396 + i * 4;
            EEPROM.write(addr, (wifi_detect_hourly_appear_[i] >> 8) & 0xFF);
            EEPROM.write(addr + 1, wifi_detect_hourly_appear_[i] & 0xFF);
            EEPROM.write(addr + 2, (wifi_detect_hourly_total_[i] >> 8) & 0xFF);
            EEPROM.write(addr + 3, wifi_detect_hourly_total_[i] & 0xFF);
        }
        requestEEPROMCommit();
    }
}

uint8_t GPIOManager::getWiFiDetectCurrentHour() {
    return wifi_detect_current_hour_;
}

uint8_t GPIOManager::getWiFiDetectCurrentInterval() {
    return wifi_detect_current_interval_;
}

float GPIOManager::getWiFiDetectHourlyProbability() {
    time_t now = time(nullptr);
    struct tm* timeinfo = localtime(&now);
    if (!timeinfo || timeinfo->tm_year < 100) return 0;
    uint8_t h = timeinfo->tm_hour;
    if (wifi_detect_hourly_total_[h] == 0) return 0;
    return (float)wifi_detect_hourly_appear_[h] / (float)wifi_detect_hourly_total_[h];
}

unsigned long GPIOManager::getWiFiDetectAutoInterval() {
    float prob = getWiFiDetectHourlyProbability();
    if (prob > 0.7f) {
        return 5000;
    } else if (prob > 0.4f) {
        return 15000;
    } else if (prob > 0.2f) {
        return 30000;
    } else {
        return 60000;
    }
}

void GPIOManager::requestEEPROMCommit() {
    eeprom_dirty_ = true;
}

void GPIOManager::flushPendingEEPROM() {
    if (!eeprom_dirty_) return;
    if (enable_pulse_active_) return;
    unsigned long now = millis();
    if (now - eeprom_last_commit_ < 5000) return;
    EEPROM.commit();
    eeprom_last_commit_ = now;
    eeprom_dirty_ = false;
}