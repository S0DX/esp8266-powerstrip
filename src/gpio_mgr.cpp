#include "gpio_mgr.h"
#include "config.h"
#include <climits>
#include <ESP8266WiFi.h>
#include "sy7t609.h"
#include "mqtt_mgr.h"
#include "energy_mgr.h"
#include "wifi_mgr.h"

bool GPIOManager::relay_master_state_ = false;
bool GPIOManager::relay_slave_state_ = false;
bool GPIOManager::locked_ = false;
GPIOManager::LEDMode GPIOManager::red_mode_ = LED_OFF;
unsigned long GPIOManager::blink_timer_ = 0;
bool GPIOManager::blink_state_ = false;
bool GPIOManager::red_led_enabled_ = true;   // 默认红灯开启

// 定时器相关
bool GPIOManager::timer_enabled_ = false;
bool GPIOManager::timer_running_ = false;
uint16_t GPIOManager::timer_duration_ = 1;
unsigned long GPIOManager::timer_start_time_ = 0;
uint8_t GPIOManager::timer_target_ = 0;   // 0=both, 1=master-only

#define EEPROM_LOCK_ADDR          200
#define EEPROM_MAGIC_LOCK         0x42
#define EEPROM_RED_LED_ADDR       203
#define EEPROM_CYCLE_MAGIC_ADDR   204
#define EEPROM_CYCLE_ENABLED_ADDR 205
#define EEPROM_CYCLE_SH_ADDR      206
#define EEPROM_CYCLE_SM_ADDR      207
#define EEPROM_CYCLE_EH_ADDR      208
#define EEPROM_CYCLE_EM_ADDR      209
#define EEPROM_CYCLE_MAGIC        0xC8
#define EEPROM_CYCLE_MAGIC_V2     0xC9
#define EEPROM_CYCLE_MAGIC_V3     0xCA
#define EEPROM_CYCLE_COUNT_ADDR   206
// V3: 最多 6 个时段，存放在 726-749（V2 的 492-503 仅用于迁移读取）
#define EEPROM_CYCLE_PERIODS_ADDR 726
#define EEPROM_CYCLE_PERIODS_ADDR_OLD 492

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
GPIOManager::BillingMode GPIOManager::billing_mode_ = BILLING_ENERGY;
float GPIOManager::billing_threshold_ = 10.0f;
uint16_t GPIOManager::billing_threshold_money_cents_ = 500;   // 默认 5.00 元
uint16_t GPIOManager::billing_threshold_time_minutes_ = 60;   // 默认 60 分钟
float GPIOManager::billing_start_energy_ = 0.0f;
uint32_t GPIOManager::billing_start_time_ = 0;
unsigned long GPIOManager::billing_start_millis_ = 0;
GPIOManager::BillingStopReason GPIOManager::billing_stop_reason_ = BILLING_STOP_NONE;
float GPIOManager::energy_price_ = 0.6f;

// WiFi 检测功能相关
bool GPIOManager::wifi_detect_enabled_ = false;
char GPIOManager::wifi_detect_target_[33] = {0};
char GPIOManager::wifi_detect_mac_[18] = {0};
uint16_t GPIOManager::wifi_detect_timeout_ = 60;
bool GPIOManager::wifi_detect_link_timer_ = false;
bool GPIOManager::wifi_was_present_ = false;
int8_t GPIOManager::wifi_detect_rssi_threshold_ = -70;
uint8_t GPIOManager::wifi_detect_scan_speed_ = 1;
bool GPIOManager::wifi_detect_power_checking_ = false;
uint8_t GPIOManager::wifi_detect_current_hour_ = 0;
uint8_t GPIOManager::wifi_detect_current_interval_ = 15;
bool GPIOManager::wifi_detect_only_master_ = false;
uint8_t GPIOManager::timer_source_ = 0;
bool GPIOManager::eeprom_dirty_ = false;
unsigned long GPIOManager::enable_pulse_start_ = 0;
bool GPIOManager::enable_pulse_active_ = false;
static unsigned long eeprom_last_commit_ = 0;
static bool button_detect_scan_requested_ = false;
static uint16_t wifi_detect_hourly_appear_[24] = {0};
static uint16_t wifi_detect_hourly_total_[24] = {0};

#define EEPROM_POWER_OFF_ENABLED_ADDR 212
#define EEPROM_POWER_OFF_THRESHOLD_ADDR 213
// EEPROM_BILLING_ENABLED_ADDR / EEPROM_BILLING_THRESHOLD_KWH_ADDR 已在 config.h 定义（214/215），此处不再重复
#define EEPROM_CONFIG_MAGIC_ADDR 199
#define EEPROM_CONFIG_MAGIC 0xD7

static void migrateBillingHistory() {
    // 若新地址已存在有效计数，说明已迁移过
    uint8_t newCount = EEPROM.read(EEPROM_BILLING_HISTORY_COUNT_ADDR);
    if (newCount <= EEPROM_BILLING_HISTORY_MAX) return;

    uint8_t oldCount = EEPROM.read(EEPROM_BILLING_HISTORY_COUNT_ADDR_OLD);
    if (oldCount > EEPROM_BILLING_HISTORY_MAX) return;

    for (int i = 0; i < EEPROM_BILLING_HISTORY_MAX * EEPROM_BILLING_RECORD_SIZE; i++) {
        EEPROM.write(EEPROM_BILLING_HISTORY_ADDR + i, EEPROM.read(EEPROM_BILLING_HISTORY_ADDR_OLD + i));
    }
    EEPROM.write(EEPROM_BILLING_HISTORY_COUNT_ADDR, oldCount);
    EEPROM.write(EEPROM_BILLING_HISTORY_COUNT_ADDR_OLD, 0xFF);
    EEPROM.commit();
    Serial.println("[GPIO] Billing history migrated to new EEPROM layout");
}

void GPIOManager::init() {
    initRelayPins();
    initLEDPins();
    initButtonPin();

    migrateBillingHistory();

    if (EEPROM.read(EEPROM_CONFIG_MAGIC_ADDR) != EEPROM_CONFIG_MAGIC) {
        EEPROM.write(EEPROM_POWER_OFF_ENABLED_ADDR, 0);
        EEPROM.write(EEPROM_POWER_OFF_THRESHOLD_ADDR, 50);
        EEPROM.write(EEPROM_BILLING_ENABLED_ADDR, 0);
        uint16_t bt = 1000;
        EEPROM.write(EEPROM_BILLING_THRESHOLD_KWH_ADDR, bt >> 8);
        EEPROM.write(EEPROM_BILLING_THRESHOLD_KWH_ADDR + 1, bt & 0xFF);
        EEPROM.write(EEPROM_WIFIDETECT_ENABLED, 0);
        EEPROM.write(EEPROM_WIFIDETECT_ONLY_MASTER, 0);
        EEPROM.write(EEPROM_WIFIDETECT_MAGIC, 0);
        EEPROM.write(EEPROM_BUTTON_AUTO_TIMER_ADDR, 0);
        EEPROM.write(EEPROM_BUTTON_DETECT_ADDR, 0);
        EEPROM.write(EEPROM_CARD_VISIBILITY_ADDR, 0x3F);
        EEPROM.write(EEPROM_CONFIG_MAGIC_ADDR, EEPROM_CONFIG_MAGIC);
        EEPROM.commit();
        DBG_PRINTF("[GPIO] EEPROM config initialized with defaults\n");
    } else {
        uint8_t v256 = EEPROM.read(EEPROM_WIFIDETECT_ONLY_MASTER);
        if (v256 != 0 && v256 != 1) {
            EEPROM.write(EEPROM_WIFIDETECT_ONLY_MASTER, 0);
            EEPROM.commit();
            DBG_PRINTF("[GPIO] EEPROM addr %d was invalid (%d), reset to 0\n", EEPROM_WIFIDETECT_ONLY_MASTER, v256);
        }
    }

    if (EEPROM.read(EEPROM_LOCK_ADDR) == EEPROM_MAGIC_LOCK) {
        locked_ = true;
        DBG_PRINTF("[GPIO] Lock mode enabled\n");
    }
    
    // 加载定时器设置（已迁移到分钟格式）
    timer_enabled_ = (EEPROM.read(EEPROM_TIMER_ENABLED_ADDR) == 1);
    if (EEPROM.read(EEPROM_TIMER_VERSION_ADDR) == EEPROM_TIMER_VERSION_MAGIC) {
        timer_duration_ = (EEPROM.read(EEPROM_TIMER_DURATION_MIN_ADDR) << 8) | EEPROM.read(EEPROM_TIMER_DURATION_MIN_ADDR + 1);
    } else {
        // 从旧格式（小时）迁移到分钟
        uint8_t old_hours = EEPROM.read(EEPROM_TIMER_DURATION_ADDR);
        if (old_hours == 0 || old_hours > 24) old_hours = 1;
        timer_duration_ = old_hours * 60U;
        EEPROM.write(EEPROM_TIMER_DURATION_MIN_ADDR, timer_duration_ >> 8);
        EEPROM.write(EEPROM_TIMER_DURATION_MIN_ADDR + 1, timer_duration_ & 0xFF);
        EEPROM.write(EEPROM_TIMER_VERSION_ADDR, EEPROM_TIMER_VERSION_MAGIC);
        EEPROM.commit();
        DBG_PRINTF("[GPIO] Timer duration migrated: %dh -> %dmin\n", old_hours, timer_duration_);
    }
    if (timer_duration_ == 0 || timer_duration_ > 1440) timer_duration_ = 60;
    
    // 加载红灯设置（默认开启，0x00=明确关闭，其他=开启）
    uint8_t redLedVal = EEPROM.read(EEPROM_RED_LED_ADDR);
    red_led_enabled_ = (redLedVal != 0x00);  // 未初始化(0xFF)或明确开启都默认开启

    if (EEPROM.read(EEPROM_CYCLE_MAGIC_ADDR) == EEPROM_CYCLE_MAGIC_V3) {
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
    } else if (EEPROM.read(EEPROM_CYCLE_MAGIC_ADDR) == EEPROM_CYCLE_MAGIC_V2) {
        cycle_enabled_ = (EEPROM.read(EEPROM_CYCLE_ENABLED_ADDR) == 1);
        cycle_period_count_ = EEPROM.read(EEPROM_CYCLE_COUNT_ADDR);
        if (cycle_period_count_ > 3) cycle_period_count_ = 0;
        for (uint8_t i = 0; i < cycle_period_count_; i++) {
            int addr = EEPROM_CYCLE_PERIODS_ADDR_OLD + i * 4;
            cycle_periods_[i].sh = EEPROM.read(addr);
            cycle_periods_[i].sm = EEPROM.read(addr + 1);
            cycle_periods_[i].eh = EEPROM.read(addr + 2);
            cycle_periods_[i].em = EEPROM.read(addr + 3);
        }
        EEPROM.write(EEPROM_CYCLE_MAGIC_ADDR, EEPROM_CYCLE_MAGIC_V3);
        EEPROM.write(EEPROM_CYCLE_COUNT_ADDR, cycle_period_count_);
        for (uint8_t i = 0; i < MAX_CYCLE_PERIODS; i++) {
            int addr = EEPROM_CYCLE_PERIODS_ADDR + i * 4;
            EEPROM.write(addr, cycle_periods_[i].sh);
            EEPROM.write(addr + 1, cycle_periods_[i].sm);
            EEPROM.write(addr + 2, cycle_periods_[i].eh);
            EEPROM.write(addr + 3, cycle_periods_[i].em);
        }
        EEPROM.commit();
        DBG_PRINTF("[GPIO] Cycle data migrated V2 -> V3 (max %d periods)\n", MAX_CYCLE_PERIODS);
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
        EEPROM.write(EEPROM_CYCLE_MAGIC_ADDR, EEPROM_CYCLE_MAGIC_V3);
        EEPROM.write(EEPROM_CYCLE_COUNT_ADDR, cycle_period_count_);
        for (uint8_t i = 0; i < MAX_CYCLE_PERIODS; i++) {
            int addr = EEPROM_CYCLE_PERIODS_ADDR + i * 4;
            EEPROM.write(addr, cycle_periods_[i].sh);
            EEPROM.write(addr + 1, cycle_periods_[i].sm);
            EEPROM.write(addr + 2, cycle_periods_[i].eh);
            EEPROM.write(addr + 3, cycle_periods_[i].em);
        }
        EEPROM.commit();
        DBG_PRINTF("[GPIO] Cycle data migrated V1 -> V3\n");
    } else {
        cycle_enabled_ = false;
        cycle_period_count_ = 0;
    }
    cycle_start_h_ = cycle_period_count_ > 0 ? cycle_periods_[0].sh : 0;
    cycle_start_m_ = cycle_period_count_ > 0 ? cycle_periods_[0].sm : 0;
    cycle_end_h_ = cycle_period_count_ > 0 ? cycle_periods_[0].eh : 0;
    cycle_end_m_ = cycle_period_count_ > 0 ? cycle_periods_[0].em : 0;

    // 加载 WiFi 检测功能设置（默认关闭）
    uint8_t wdEn = EEPROM.read(EEPROM_WIFIDETECT_ENABLED);
    wifi_detect_enabled_ = (wdEn == 1);
    wifi_detect_rssi_threshold_ = (int8_t)EEPROM.read(EEPROM_WIFIDETECT_RSSI);
    if (wifi_detect_rssi_threshold_ == 0 || wifi_detect_rssi_threshold_ > -40) {
        wifi_detect_rssi_threshold_ = -70;  // 默认 -70dBm
    }

    // 向后兼容：旧固件将 WiFi 检测目标 magic 存在 292，新位置为 286
    if (EEPROM.read(EEPROM_WIFIDETECT_MAGIC) != EEPROM_WIFIDETECT_MAGIC_VAL &&
        EEPROM.read(EEPROM_WIFIDETECT_MAGIC_ADDR_OLD) == EEPROM_WIFIDETECT_MAGIC_VAL) {
        EEPROM.write(EEPROM_WIFIDETECT_MAGIC, EEPROM_WIFIDETECT_MAGIC_VAL);
        EEPROM.write(EEPROM_WIFIDETECT_MAGIC_ADDR_OLD, 0);
        EEPROM.commit();
        DBG_PRINTF("[GPIO] WiFi detect magic migrated from 292 to %d\n", EEPROM_WIFIDETECT_MAGIC);
    }

    // 读取检测目标，增加 magic 标记和全字符串校验
    bool target_valid = (EEPROM.read(EEPROM_WIFIDETECT_MAGIC) == EEPROM_WIFIDETECT_MAGIC_VAL);
    if (target_valid) {
        char tmp[33] = {0};
        for (int i = 0; i < 32; i++) {
            tmp[i] = (char)EEPROM.read(EEPROM_WIFIDETECT_TARGET + i);
        }
        tmp[32] = 0;
        // SSID 允许可打印 ASCII；空目标或非法字符均视为无效
        if (tmp[0] != 0 && tmp[0] != 0xFF) {
            target_valid = true;
            for (int i = 0; i < 32 && tmp[i] != 0; i++) {
                if (tmp[i] < 32 || tmp[i] > 126) {
                    target_valid = false;
                    break;
                }
            }
            if (target_valid) {
                strncpy(wifi_detect_target_, tmp, 32);
                wifi_detect_target_[32] = 0;
            }
        } else {
            target_valid = false;
        }
    }
    if (!target_valid) {
        memset(wifi_detect_target_, 0, sizeof(wifi_detect_target_));
        // 同步清除 EEPROM 中的脏数据
        for (int i = 0; i < 32; i++) {
            EEPROM.write(EEPROM_WIFIDETECT_TARGET + i, 0);
        }
        EEPROM.write(EEPROM_WIFIDETECT_MAGIC, 0);
        requestEEPROMCommit();
    }

    wifi_detect_link_timer_ = (EEPROM.read(EEPROM_WIFIDETECT_LINK_TIMER) == 1);
    wifi_detect_scan_speed_ = EEPROM.read(EEPROM_WIFIDETECT_SCAN_SPEED);
    if (wifi_detect_scan_speed_ > 3) wifi_detect_scan_speed_ = 1;
    wifi_detect_only_master_ = (EEPROM.read(EEPROM_WIFIDETECT_ONLY_MASTER) == 1);

    uint16_t timeout_raw = (EEPROM.read(EEPROM_WIFIDETECT_TIMEOUT + 1) << 8) | EEPROM.read(EEPROM_WIFIDETECT_TIMEOUT);
    if (timeout_raw >= 10 && timeout_raw <= 3600) {
        wifi_detect_timeout_ = timeout_raw;
    }

    for (int h = 0; h < 24; h++) {
        int addr = EEPROM_WIFIDETECT_HOURLY + h * 4;
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
    if (thresh_raw > 0 && thresh_raw <= 100) {
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
    uint16_t billing_raw = (EEPROM.read(EEPROM_BILLING_THRESHOLD_KWH_ADDR) << 8) | EEPROM.read(EEPROM_BILLING_THRESHOLD_KWH_ADDR + 1);
    if (billing_raw > 0 && billing_raw <= 5000) {
        billing_threshold_ = billing_raw / 100.0f;
    } else {
        billing_threshold_ = 10.0f;
        uint16_t raw = (uint16_t)(billing_threshold_ * 100);
        EEPROM.write(EEPROM_BILLING_THRESHOLD_KWH_ADDR, (raw >> 8) & 0xFF);
        EEPROM.write(EEPROM_BILLING_THRESHOLD_KWH_ADDR + 1, raw & 0xFF);
        EEPROM.commit();
        DBG_PRINTF("[GPIO] Billing threshold EEPROM invalid, using default: %.2f kWh\n", billing_threshold_);
    }

    // 电价（与 WiFi 检测电价共用地址 290-291）
    uint16_t price_raw = (EEPROM.read(EEPROM_WIFIDETECT_PRICE) << 8) | EEPROM.read(EEPROM_WIFIDETECT_PRICE + 1);
    if (price_raw > 0 && price_raw <= 2000) {
        energy_price_ = price_raw / 100.0f;
    } else {
        energy_price_ = 0.6f;
    }

    // 计费配置 magic 初始化和向后兼容
    if (EEPROM.read(EEPROM_BILLING_CONFIG_MAGIC_ADDR) != EEPROM_BILLING_CONFIG_MAGIC) {
        billing_mode_ = BILLING_ENERGY;
        billing_threshold_money_cents_ = 500;   // 5.00 元
        billing_threshold_time_minutes_ = 60;   // 60 分钟
        billing_start_time_ = 0;
        billing_stop_reason_ = BILLING_STOP_NONE;
        EEPROM.write(EEPROM_BILLING_MODE_ADDR, (uint8_t)billing_mode_);
        EEPROM.write(EEPROM_BILLING_THRESHOLD_MONEY_ADDR, (billing_threshold_money_cents_ >> 8) & 0xFF);
        EEPROM.write(EEPROM_BILLING_THRESHOLD_MONEY_ADDR + 1, billing_threshold_money_cents_ & 0xFF);
        EEPROM.write(EEPROM_BILLING_THRESHOLD_TIME_ADDR, (billing_threshold_time_minutes_ >> 8) & 0xFF);
        EEPROM.write(EEPROM_BILLING_THRESHOLD_TIME_ADDR + 1, billing_threshold_time_minutes_ & 0xFF);
        EEPROM.write(EEPROM_BILLING_STOP_REASON_ADDR, (uint8_t)billing_stop_reason_);
        EEPROM.write(EEPROM_BILLING_HISTORY_COUNT_ADDR, 0);
        EEPROM.write(EEPROM_BILLING_CONFIG_MAGIC_ADDR, EEPROM_BILLING_CONFIG_MAGIC);
        EEPROM.commit();
        DBG_PRINTF("[GPIO] Billing config initialized with defaults\n");
    } else {
        uint8_t mode = EEPROM.read(EEPROM_BILLING_MODE_ADDR);
        if (mode <= BILLING_TIME) {
            billing_mode_ = (BillingMode)mode;
        } else {
            billing_mode_ = BILLING_ENERGY;
        }
        billing_threshold_money_cents_ = (EEPROM.read(EEPROM_BILLING_THRESHOLD_MONEY_ADDR) << 8) | EEPROM.read(EEPROM_BILLING_THRESHOLD_MONEY_ADDR + 1);
        if (billing_threshold_money_cents_ == 0 || billing_threshold_money_cents_ > 65535) {
            billing_threshold_money_cents_ = 500;
        }
        billing_threshold_time_minutes_ = (EEPROM.read(EEPROM_BILLING_THRESHOLD_TIME_ADDR) << 8) | EEPROM.read(EEPROM_BILLING_THRESHOLD_TIME_ADDR + 1);
        if (billing_threshold_time_minutes_ == 0 || billing_threshold_time_minutes_ > 65535) {
            billing_threshold_time_minutes_ = 60;
        }
        billing_stop_reason_ = (BillingStopReason)EEPROM.read(EEPROM_BILLING_STOP_REASON_ADDR);
        if (billing_stop_reason_ > BILLING_STOP_MANUAL) {
            billing_stop_reason_ = BILLING_STOP_NONE;
        }
    }

    if (billing_enabled_) {
        uint8_t bse_buf[4] = {0};
        for (int i = 0; i < 4; i++) bse_buf[i] = EEPROM.read(EEPROM_BILLING_START_ENERGY_ADDR + i);
        memcpy(&billing_start_energy_, bse_buf, sizeof(float));
        if (billing_start_energy_ < 0 || billing_start_energy_ > 999999) {
            billing_start_energy_ = 0;
        }
        billing_start_time_ = 0;
        for (int i = 0; i < 4; i++) {
            billing_start_time_ = (billing_start_time_ << 8) | EEPROM.read(EEPROM_BILLING_START_TIME_ADDR + i);
        }
        if (billing_start_time_ == 0) {
            // 旧固件没有记录启动时间，使用当前时间或 0
            billing_start_time_ = (uint32_t)time(nullptr);
            if (billing_start_time_ < 1600000000UL) billing_start_time_ = 0;
        }
    }

    DBG_PRINTF("[GPIO] Billing: enabled=%s, mode=%d, threshold=%.2fkWh/%d分/%d分, price=%.2f\n",
        billing_enabled_ ? "yes" : "no", (int)billing_mode_,
        billing_threshold_, billing_threshold_money_cents_, billing_threshold_time_minutes_,
        energy_price_);
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
    // 状态未变化时跳过所有操作，避免无预期的继电器使能脉冲声，也不终止倒计时
    if (on == relay_master_state_) {
        return;
    }
    // 任何手动继电器操作都终止倒计时（含关闭：master-only 模式下超时关主也走此路径）
    if (timer_running_) {
        DBG_PRINTF("[GPIO] Stopping timer due to master relay change\n");
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
    // 状态未变化时跳过所有操作，避免无预期的继电器使能脉冲声，也不终止倒计时
    if (on == relay_slave_state_) {
        return;
    }
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

void GPIOManager::setRelaySlaveDuringTimer() {
    // 倒计时运行中安全打开从继电器，不终止倒计时，仅同步继电器状态
    if (!timer_running_) {
        setRelaySlave(true);
        return;
    }
    if (relay_slave_state_) return;
    if (!relay_master_state_) {
        // 倒计时运行时主继电器必为开启，若异常关闭则直接打开
        relay_master_state_ = true;
        digitalWrite(RELAY_MASTER_PIN, LOW);
    }
    relay_slave_state_ = true;
    digitalWrite(RELAY_SLAVE_PIN, LOW);
    triggerRelayEnable();
    setLEDWhite(true);
    DBG_PRINTF("[GPIO] Slave relay ON during timer (target switched to both)\n");
    MQTTManager::requestPublish();
}

void GPIOManager::setRelays(bool master, bool slave) {
    // 状态未变化时跳过所有操作，避免无预期的继电器使能脉冲声，也不终止倒计时
    if (relay_master_state_ == master && relay_slave_state_ == slave) {
        return;
    }
    // 任何手动/外部继电器操作都终止倒计时
    if (timer_running_) {
        DBG_PRINTF("[GPIO] Stopping timer due to relay change\n");
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
    (void)blink;  // 保留参数以兼容现有调用，但蓝灯模式不再维护状态
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
    bool timer_active = (timer_running_ && timer_enabled_);

    // 倒计时 LED 指示（问题 M）：
    //   master-only(target=1) → 蓝灯闪烁
    //   both(target=0)        → 白灯闪烁
    if (timer_active) {
        if (millisDiff(millis(), blink_timer_) > 500) {
            blink_timer_ = millis();
            blink_state_ = !blink_state_;
            if (timer_target_ == 1) {
                digitalWrite(LED_BLUE_PIN, blink_state_ ? LOW : HIGH);
            } else {
                digitalWrite(LED_WHITE_PIN, blink_state_ ? LOW : HIGH);
            }
        }
    }

    // 蓝灯：master-only 倒计时由上面接管，否则反映 cycle 状态
    if (!(timer_active && timer_target_ == 1)) {
        digitalWrite(LED_BLUE_PIN, cycle_enabled_ ? LOW : HIGH);
    }

    // 白灯：both 倒计时由上面接管，否则反映从继电器状态
    if (!(timer_active && timer_target_ == 0)) {
        digitalWrite(LED_WHITE_PIN, relay_slave_state_ ? LOW : HIGH);
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
        if (timer_running_) {
            DBG_PRINTF("[GPIO] Stopping timer due to timer disabled\n");
            stopTimer();
        }
    }
    EEPROM.write(EEPROM_TIMER_ENABLED_ADDR, enabled ? 1 : 0);
    requestEEPROMCommit();
    DBG_PRINTF("[GPIO] Timer enabled: %s\n", enabled ? "yes" : "no");
}

bool GPIOManager::isTimerEnabled() {
    return timer_enabled_;
}

void GPIOManager::setTimerDuration(uint16_t minutes) {
    if (minutes < 1 || minutes > 1440) {
        minutes = 60;
    }
    timer_duration_ = minutes;
    EEPROM.write(EEPROM_TIMER_DURATION_MIN_ADDR, minutes >> 8);
    EEPROM.write(EEPROM_TIMER_DURATION_MIN_ADDR + 1, minutes & 0xFF);
    requestEEPROMCommit();
    DBG_PRINTF("[GPIO] Timer duration set to: %dmin\n", minutes);
}

uint16_t GPIOManager::getTimerDuration() {
    return timer_duration_;
}

void GPIOManager::startTimer(uint8_t target) {
    if (!timer_enabled_ || timer_running_) return;
    if (timer_duration_ == 0) timer_duration_ = 1;
    timer_target_ = target;
    timer_running_ = true;
    timer_start_time_ = millis();
    relay_master_state_ = true;
    digitalWrite(RELAY_MASTER_PIN, LOW);
    if (target == 1) {
        // master-only：不动从继电器
        DBG_PRINTF("[GPIO] Timer started: %dmin (master-only)\n", timer_duration_);
    } else {
        // both：强开从继电器
        relay_slave_state_ = true;
        digitalWrite(RELAY_SLAVE_PIN, LOW);
        DBG_PRINTF("[GPIO] Timer started: %dmin (both)\n", timer_duration_);
    }
    triggerRelayEnable();
}

void GPIOManager::setTimerTarget(uint8_t target) {
    if (timer_running_) {
        timer_target_ = target;
        DBG_PRINTF("[GPIO] Timer target switched to %s\n", target == 1 ? "master-only" : "both");
    }
}

void GPIOManager::stopTimer() {
    if (!timer_running_) return;
    uint8_t stopped_source = timer_source_;
    uint8_t stopped_target = timer_target_;
    timer_running_ = false;
    timer_start_time_ = 0;
    timer_source_ = 0;     // P0-A: 重置 source，避免 WiFi 检测扫描被永久抑制
    timer_target_ = 0;     // 重置 target
    relay_master_state_ = false;
    relay_slave_state_ = false;
    digitalWrite(RELAY_MASTER_PIN, HIGH);
    digitalWrite(RELAY_SLAVE_PIN, HIGH);
    triggerRelayEnable();
    setLEDWhite(false);
    DBG_PRINTF("[GPIO] Timer stopped (source=%d, target=%d)\n", stopped_source, stopped_target);
}

bool GPIOManager::isTimerRunning() {
    return timer_running_;
}

unsigned long GPIOManager::getTimerRemaining() {
    if (!timer_running_) {
        return 0;
    }
    unsigned long elapsed = millisDiff(millis(), timer_start_time_) / 1000;
    unsigned long total = timer_duration_ * 60UL;
    if (elapsed >= total) {
        return 0;
    }
    return total - elapsed;
}

void GPIOManager::handleTimer() {
    if (timer_running_ && timer_enabled_) {
        unsigned long elapsed = millisDiff(millis(), timer_start_time_) / 1000;
        unsigned long total = timer_duration_ * 60UL;
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
    EEPROM.write(EEPROM_CYCLE_MAGIC_ADDR, EEPROM_CYCLE_MAGIC_V3);
    EEPROM.write(EEPROM_CYCLE_ENABLED_ADDR, enabled ? 1 : 0);
    requestEEPROMCommit();
    DBG_PRINTF("[GPIO] Cycle enabled: %s\n", enabled ? "yes" : "no");
}

bool GPIOManager::isCycleEnabled() {
    return cycle_enabled_;
}

bool GPIOManager::isCycleActive() {
    return cycle_is_active_;
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
    EEPROM.write(EEPROM_CYCLE_MAGIC_ADDR, EEPROM_CYCLE_MAGIC_V3);
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
    EEPROM.write(EEPROM_CYCLE_MAGIC_ADDR, EEPROM_CYCLE_MAGIC_V3);
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
    EEPROM.write(EEPROM_CYCLE_MAGIC_ADDR, EEPROM_CYCLE_MAGIC_V3);
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
    EEPROM.write(EEPROM_CYCLE_MAGIC_ADDR, EEPROM_CYCLE_MAGIC_V3);
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
    if (threshold > 1.0f) threshold = 1.0f;
    power_off_threshold_ = threshold;
    EEPROM.write(EEPROM_POWER_OFF_THRESHOLD_ADDR, (uint8_t)(threshold * 100));
    requestEEPROMCommit();
    DBG_PRINTF("[GPIO] Power off threshold: %.2f W\n", threshold);
}

float GPIOManager::getPowerOffThreshold() {
    return power_off_threshold_;
}

void GPIOManager::setBillingEnabled(bool enabled) {
    // 计费供电与拔除断电互斥：计费开启时临时禁用拔除断电，计费停止时恢复
    if (enabled && power_off_enabled_) {
        power_off_enabled_ = false;
        EEPROM.write(EEPROM_POWER_OFF_ENABLED_ADDR, 0);
    } else if (!enabled && !power_off_enabled_ && EEPROM.read(EEPROM_POWER_OFF_ENABLED_ADDR) == 1) {
        // 如果用户原本配置了拔除断电，停止计费时恢复
        power_off_enabled_ = true;
    }

    if (enabled && !billing_enabled_) {
        // 开始新会话：清空历史记录，从 0 开始计费
        EEPROM.write(EEPROM_BILLING_HISTORY_COUNT_ADDR, 0);
        for (int i = 0; i < EEPROM_BILLING_HISTORY_MAX * EEPROM_BILLING_RECORD_SIZE; i++) {
            EEPROM.write(EEPROM_BILLING_HISTORY_ADDR + i, 0);
        }

        // 记录起始电量和起始时间，并开启继电器
        billing_start_energy_ = EnergyManager::getTotalEnergy();
        billing_start_time_ = (uint32_t)time(nullptr);
        if (billing_start_time_ < 1600000000UL) billing_start_time_ = 0;
        billing_start_millis_ = millis();
        billing_stop_reason_ = BILLING_STOP_NONE;
        uint8_t bse_buf[4] = {0};
        memcpy(bse_buf, &billing_start_energy_, sizeof(float));
        for (int i = 0; i < 4; i++) EEPROM.write(EEPROM_BILLING_START_ENERGY_ADDR + i, bse_buf[i]);
        for (int i = 0; i < 4; i++) {
            EEPROM.write(EEPROM_BILLING_START_TIME_ADDR + i, (billing_start_time_ >> (8 * (3 - i))) & 0xFF);
        }
        EEPROM.write(EEPROM_BILLING_STOP_REASON_ADDR, (uint8_t)billing_stop_reason_);
        // 自动开启供电（会终止倒计时）
        if (timer_running_) {
            DBG_PRINTF("[GPIO] Stopping timer due to billing session start\n");
        }
        setRelays(true, true);
        DBG_PRINTF("[GPIO] Billing session started, energy=%.2fWh, time=%u\n", billing_start_energy_, billing_start_time_);
    }

    billing_enabled_ = enabled;
    EEPROM.write(EEPROM_BILLING_ENABLED_ADDR, enabled ? 1 : 0);
    requestEEPROMCommit();
    DBG_PRINTF("[GPIO] Billing enabled: %s, mode=%d, threshold=%.2fkWh/%d分/%d分\n",
        enabled ? "yes" : "no", (int)billing_mode_,
        billing_threshold_, billing_threshold_money_cents_, billing_threshold_time_minutes_);
}

bool GPIOManager::isBillingEnabled() {
    return billing_enabled_;
}

void GPIOManager::setBillingMode(BillingMode mode) {
    if (mode > BILLING_TIME) mode = BILLING_ENERGY;
    billing_mode_ = mode;
    EEPROM.write(EEPROM_BILLING_MODE_ADDR, (uint8_t)mode);
    requestEEPROMCommit();
    DBG_PRINTF("[GPIO] Billing mode: %d\n", (int)mode);
}

GPIOManager::BillingMode GPIOManager::getBillingMode() {
    return billing_mode_;
}

void GPIOManager::setBillingThreshold(float kwh) {
    if (kwh < 0.01f) kwh = 0.01f;
    if (kwh > 50) kwh = 50;
    billing_threshold_ = kwh;
    uint16_t raw = (uint16_t)(kwh * 100);
    EEPROM.write(EEPROM_BILLING_THRESHOLD_KWH_ADDR, (raw >> 8) & 0xFF);
    EEPROM.write(EEPROM_BILLING_THRESHOLD_KWH_ADDR + 1, raw & 0xFF);
    requestEEPROMCommit();
    DBG_PRINTF("[GPIO] Billing energy threshold: %.2f kWh\n", kwh);
}

float GPIOManager::getBillingThreshold() {
    return billing_threshold_;
}

void GPIOManager::setBillingThresholdMoney(float yuan) {
    uint16_t cents = (uint16_t)(yuan * 100.0f);
    if (cents < 1) cents = 1;
    if (cents > 65535) cents = 65535;
    billing_threshold_money_cents_ = cents;
    EEPROM.write(EEPROM_BILLING_THRESHOLD_MONEY_ADDR, (cents >> 8) & 0xFF);
    EEPROM.write(EEPROM_BILLING_THRESHOLD_MONEY_ADDR + 1, cents & 0xFF);
    requestEEPROMCommit();
    DBG_PRINTF("[GPIO] Billing money threshold: %.2f yuan\n", cents / 100.0f);
}

float GPIOManager::getBillingThresholdMoney() {
    return billing_threshold_money_cents_ / 100.0f;
}

void GPIOManager::setBillingThresholdTime(uint16_t minutes) {
    if (minutes < 1) minutes = 1;
    if (minutes > 65535) minutes = 65535;
    billing_threshold_time_minutes_ = minutes;
    EEPROM.write(EEPROM_BILLING_THRESHOLD_TIME_ADDR, (minutes >> 8) & 0xFF);
    EEPROM.write(EEPROM_BILLING_THRESHOLD_TIME_ADDR + 1, minutes & 0xFF);
    requestEEPROMCommit();
    DBG_PRINTF("[GPIO] Billing time threshold: %d minutes\n", minutes);
}

uint16_t GPIOManager::getBillingThresholdTime() {
    return billing_threshold_time_minutes_;
}

float GPIOManager::getBillingStartEnergy() {
    return billing_start_energy_;
}

void GPIOManager::setBillingStartEnergy(float energy) {
    billing_start_energy_ = energy;
    uint8_t bse_buf[4] = {0};
    memcpy(bse_buf, &billing_start_energy_, sizeof(float));
    for (int i = 0; i < 4; i++) EEPROM.write(EEPROM_BILLING_START_ENERGY_ADDR + i, bse_buf[i]);
    requestEEPROMCommit();
}

GPIOManager::BillingStopReason GPIOManager::getBillingStopReason() {
    return billing_stop_reason_;
}

void GPIOManager::setBillingStopReason(BillingStopReason reason) {
    if (reason > BILLING_STOP_MANUAL) reason = BILLING_STOP_NONE;
    billing_stop_reason_ = reason;
    EEPROM.write(EEPROM_BILLING_STOP_REASON_ADDR, (uint8_t)reason);
    requestEEPROMCommit();
}

float GPIOManager::getBillingUsedEnergy() {
    return EnergyManager::getTotalEnergy() - billing_start_energy_;
}

float GPIOManager::getBillingUsedMoney() {
    return getBillingUsedEnergy() / 1000.0f * energy_price_;
}

uint32_t GPIOManager::getBillingUsedMinutes() {
    if (billing_start_time_ > 0) {
        uint32_t now = (uint32_t)time(nullptr);
        if (now >= billing_start_time_) {
            return (now - billing_start_time_) / 60;
        }
    }
    // 无有效网络时间时，使用 millis() 作为备用计时
    if (billing_start_millis_ > 0) {
        return (uint32_t)(millisDiff(millis(), billing_start_millis_) / 60000UL);
    }
    return 0;
}

float GPIOManager::getEnergyPrice() {
    return energy_price_;
}

void GPIOManager::setEnergyPrice(float price) {
    if (price < 0.1f) price = 0.1f;
    if (price > 10.0f) price = 10.0f;
    energy_price_ = price;
    uint16_t raw = (uint16_t)(price * 100);
    EEPROM.write(EEPROM_WIFIDETECT_PRICE, (raw >> 8) & 0xFF);
    EEPROM.write(EEPROM_WIFIDETECT_PRICE + 1, raw & 0xFF);
    requestEEPROMCommit();
}

void GPIOManager::stopBilling(BillingStopReason reason) {
    if (!billing_enabled_) return;
    recordBillingHistory();
    billing_enabled_ = false;
    billing_stop_reason_ = reason;
    EEPROM.write(EEPROM_BILLING_ENABLED_ADDR, 0);
    EEPROM.write(EEPROM_BILLING_STOP_REASON_ADDR, (uint8_t)reason);
    requestEEPROMCommit();
    DBG_PRINTF("[GPIO] Billing stopped, reason=%d, used=%.2fWh, cost=%.2f\n",
        (int)reason, getBillingUsedEnergy(), getBillingUsedMoney());
}

void GPIOManager::recordBillingHistory() {
    if (billing_start_time_ == 0) return;
    uint8_t count = EEPROM.read(EEPROM_BILLING_HISTORY_COUNT_ADDR);
    if (count >= EEPROM_BILLING_HISTORY_MAX) {
        // 队列满时移除最旧记录（前移）
        for (uint8_t i = 1; i < EEPROM_BILLING_HISTORY_MAX; i++) {
            int src = EEPROM_BILLING_HISTORY_ADDR + (i - 1) * EEPROM_BILLING_RECORD_SIZE;
            int dst = EEPROM_BILLING_HISTORY_ADDR + i * EEPROM_BILLING_RECORD_SIZE;
            for (uint8_t j = 0; j < EEPROM_BILLING_RECORD_SIZE; j++) {
                EEPROM.write(dst + j, EEPROM.read(src + j));
            }
        }
        count = EEPROM_BILLING_HISTORY_MAX - 1;
    }
    int addr = EEPROM_BILLING_HISTORY_ADDR + count * EEPROM_BILLING_RECORD_SIZE;
    uint32_t startTime = billing_start_time_;
    float usedEnergy = getBillingUsedEnergy();
    uint16_t costCents = (uint16_t)(getBillingUsedMoney() * 100.0f);
    for (int i = 0; i < 4; i++) {
        EEPROM.write(addr + i, (startTime >> (8 * (3 - i))) & 0xFF);
    }
    uint8_t e_buf[4];
    memcpy(e_buf, &usedEnergy, sizeof(float));
    for (int i = 0; i < 4; i++) EEPROM.write(addr + 4 + i, e_buf[i]);
    EEPROM.write(addr + 8, (costCents >> 8) & 0xFF);
    EEPROM.write(addr + 9, costCents & 0xFF);
    EEPROM.write(EEPROM_BILLING_HISTORY_COUNT_ADDR, count + 1);
    requestEEPROMCommit();
    DBG_PRINTF("[GPIO] Billing history recorded: used=%.2fWh, cost=%.2f\n", usedEnergy, costCents / 100.0f);
}

uint8_t GPIOManager::getBillingHistoryCount() {
    return EEPROM.read(EEPROM_BILLING_HISTORY_COUNT_ADDR);
}

GPIOManager::BillingRecord GPIOManager::getBillingHistory(uint8_t idx) {
    BillingRecord rec = {0, 0.0f, 0};
    if (idx >= getBillingHistoryCount()) return rec;
    int addr = EEPROM_BILLING_HISTORY_ADDR + idx * EEPROM_BILLING_RECORD_SIZE;
    rec.startTime = 0;
    for (int i = 0; i < 4; i++) {
        rec.startTime = (rec.startTime << 8) | EEPROM.read(addr + i);
    }
    uint8_t e_buf[4];
    for (int i = 0; i < 4; i++) e_buf[i] = EEPROM.read(addr + 4 + i);
    memcpy(&rec.usedEnergyWh, e_buf, sizeof(float));
    rec.costCents = (EEPROM.read(addr + 8) << 8) | EEPROM.read(addr + 9);
    return rec;
}

void GPIOManager::reset() {
    // 清除人来上电相关 EEPROM 与内存状态
    EEPROM.write(EEPROM_WIFIDETECT_ENABLED, 0);
    for (int i = 0; i < 32; i++) {
        EEPROM.write(EEPROM_WIFIDETECT_TARGET + i, 0);
    }
    EEPROM.write(EEPROM_WIFIDETECT_MAGIC, 0);
    EEPROM.write(EEPROM_WIFIDETECT_TIMEOUT, 60 & 0xFF);
    EEPROM.write(EEPROM_WIFIDETECT_TIMEOUT + 1, (60 >> 8) & 0xFF);
    EEPROM.write(EEPROM_WIFIDETECT_LINK_TIMER, 0);
    EEPROM.write(EEPROM_WIFIDETECT_ONLY_MASTER, 0);
    EEPROM.write(EEPROM_WIFIDETECT_RSSI, (uint8_t)(-70));
    EEPROM.write(EEPROM_WIFIDETECT_SCAN_SPEED, 1);
    for (int h = 0; h < 24; h++) {
        int addr = EEPROM_WIFIDETECT_HOURLY + h * 4;
        EEPROM.write(addr, 0);
        EEPROM.write(addr + 1, 0);
        EEPROM.write(addr + 2, 0);
        EEPROM.write(addr + 3, 0);
    }
    EEPROM.commit();

    wifi_detect_enabled_ = false;
    memset(wifi_detect_target_, 0, sizeof(wifi_detect_target_));
    memset(wifi_detect_mac_, 0, sizeof(wifi_detect_mac_));
    wifi_detect_timeout_ = 60;
    wifi_detect_link_timer_ = false;
    wifi_was_present_ = false;
    wifi_detect_rssi_threshold_ = -70;
    wifi_detect_scan_speed_ = 1;
    wifi_detect_power_checking_ = false;
    wifi_detect_only_master_ = false;
    if (timer_source_ == 1 && timer_running_) {
        DBG_PRINTF("[GPIO] Stopping timer due to reset\n");
        stopTimer();
        timer_source_ = 0;
    }

    // 清除计费供电相关 EEPROM 与内存状态
    billing_enabled_ = false;
    billing_mode_ = BILLING_ENERGY;
    billing_threshold_ = 10.0f;
    billing_threshold_money_cents_ = 500;
    billing_threshold_time_minutes_ = 60;
    billing_start_energy_ = 0.0f;
    billing_start_time_ = 0;
    billing_stop_reason_ = BILLING_STOP_NONE;
    EEPROM.write(EEPROM_BILLING_ENABLED_ADDR, 0);
    uint16_t raw = (uint16_t)(billing_threshold_ * 100);
    EEPROM.write(EEPROM_BILLING_THRESHOLD_KWH_ADDR, (raw >> 8) & 0xFF);
    EEPROM.write(EEPROM_BILLING_THRESHOLD_KWH_ADDR + 1, raw & 0xFF);
    EEPROM.write(EEPROM_BILLING_MODE_ADDR, (uint8_t)billing_mode_);
    EEPROM.write(EEPROM_BILLING_THRESHOLD_MONEY_ADDR, (billing_threshold_money_cents_ >> 8) & 0xFF);
    EEPROM.write(EEPROM_BILLING_THRESHOLD_MONEY_ADDR + 1, billing_threshold_money_cents_ & 0xFF);
    EEPROM.write(EEPROM_BILLING_THRESHOLD_TIME_ADDR, (billing_threshold_time_minutes_ >> 8) & 0xFF);
    EEPROM.write(EEPROM_BILLING_THRESHOLD_TIME_ADDR + 1, billing_threshold_time_minutes_ & 0xFF);
    EEPROM.write(EEPROM_BILLING_STOP_REASON_ADDR, (uint8_t)billing_stop_reason_);
    EEPROM.write(EEPROM_BILLING_HISTORY_COUNT_ADDR, 0);
    for (int i = 0; i < EEPROM_BILLING_HISTORY_MAX * EEPROM_BILLING_RECORD_SIZE; i++) {
        EEPROM.write(EEPROM_BILLING_HISTORY_ADDR + i, 0);
    }
    EEPROM.commit();

    DBG_PRINTF("[GPIO] WiFi detect and billing reset to defaults\n");
}

void GPIOManager::setWiFiDetectEnabled(bool enabled) {
    wifi_detect_enabled_ = enabled;
    if (enabled) {
        wifi_was_present_ = false;
    } else {
        wifi_detect_power_checking_ = false;
        if (timer_source_ == 1 && timer_running_) {
            DBG_PRINTF("[GPIO] Stopping timer due to WiFi detect disabled\n");
            stopTimer();
            timer_source_ = 0;
            DBG_PRINTF("[GPIO] WiFi detect disabled, wifi-sourced timer stopped\n");
        }
    }
    EEPROM.write(EEPROM_WIFIDETECT_ENABLED, enabled ? 1 : 0);
    requestEEPROMCommit();
    DBG_PRINTF("[GPIO] WiFi detect: %s\n", enabled ? "enabled" : "disabled");
}

bool GPIOManager::isWiFiDetectEnabled() {
    return wifi_detect_enabled_;
}

void GPIOManager::setWiFiDetectTarget(const char* ssid) {
    if (ssid == nullptr) {
        return;
    }
    size_t len = strlen(ssid);
    if (len == 0) {
        // 清除目标
        memset(wifi_detect_target_, 0, sizeof(wifi_detect_target_));
        for (int i = 0; i < 32; i++) {
            EEPROM.write(EEPROM_WIFIDETECT_TARGET + i, 0);
        }
        EEPROM.write(EEPROM_WIFIDETECT_MAGIC, 0);
        requestEEPROMCommit();
        DBG_PRINTF("[GPIO] WiFi detect target cleared\n");
        return;
    }
    if (len > 32) len = 32;
    strncpy(wifi_detect_target_, ssid, 32);
    wifi_detect_target_[32] = 0;
    for (size_t i = 0; i < 32; i++) {
        EEPROM.write(EEPROM_WIFIDETECT_TARGET + i, (i < len) ? wifi_detect_target_[i] : 0);
    }
    EEPROM.write(EEPROM_WIFIDETECT_MAGIC, EEPROM_WIFIDETECT_MAGIC_VAL);
    requestEEPROMCommit();
    DBG_PRINTF("[GPIO] WiFi detect target: %s\n", ssid);
}

String GPIOManager::getWiFiDetectTarget() {
    return String(wifi_detect_target_);
}

void GPIOManager::setWiFiDetectTimeout(uint16_t seconds) {
    wifi_detect_timeout_ = seconds;
    EEPROM.write(EEPROM_WIFIDETECT_TIMEOUT, seconds & 0xFF);
    EEPROM.write(EEPROM_WIFIDETECT_TIMEOUT + 1, (seconds >> 8) & 0xFF);
    requestEEPROMCommit();
}

uint16_t GPIOManager::getWiFiDetectTimeout() {
    return wifi_detect_timeout_;
}

void GPIOManager::setWiFiDetectLinkTimer(bool linked) {
    wifi_detect_link_timer_ = linked;
    EEPROM.write(EEPROM_WIFIDETECT_LINK_TIMER, linked ? 1 : 0);
    requestEEPROMCommit();
}

bool GPIOManager::isWiFiDetectLinkTimer() {
    return wifi_detect_link_timer_;
}

void GPIOManager::setWiFiDetectPresent(bool present) {
    wifi_was_present_ = present;
}

bool GPIOManager::isWiFiDetectPresent() {
    return wifi_was_present_;
}

void GPIOManager::setWiFiDetectRssiThreshold(int8_t rssi) {
    wifi_detect_rssi_threshold_ = rssi;
    EEPROM.write(EEPROM_WIFIDETECT_RSSI, (uint8_t)rssi);
    requestEEPROMCommit();
}

int8_t GPIOManager::getWiFiDetectRssiThreshold() {
    return wifi_detect_rssi_threshold_;
}

void GPIOManager::setWiFiDetectScanSpeed(uint8_t speed) {
    wifi_detect_scan_speed_ = (speed > 3) ? 3 : speed;
    EEPROM.write(EEPROM_WIFIDETECT_SCAN_SPEED, wifi_detect_scan_speed_);
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

bool GPIOManager::isWiFiDetectPowerChecking() {
    return wifi_detect_power_checking_;
}

void GPIOManager::setTimerSource(uint8_t source) {
    timer_source_ = source;
}

uint8_t GPIOManager::getTimerSource() {
    return timer_source_;
}

bool GPIOManager::isButtonAutoTimerEnabled() {
    return EEPROM.read(EEPROM_BUTTON_AUTO_TIMER_ADDR) == 1;
}

void GPIOManager::setButtonAutoTimerEnabled(bool en) {
    EEPROM.write(EEPROM_BUTTON_AUTO_TIMER_ADDR, en ? 1 : 0);
    EEPROM.commit();
}

bool GPIOManager::isButtonDetectEnabled() {
    return EEPROM.read(EEPROM_BUTTON_DETECT_ADDR) == 1;
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

void GPIOManager::setWiFiDetectRelayOnlyMaster(bool only) {
    wifi_detect_only_master_ = only;
    EEPROM.write(EEPROM_WIFIDETECT_ONLY_MASTER, only ? 1 : 0);
    requestEEPROMCommit();
    DBG_PRINTF("[GPIO] WiFi detect only master: %s\n", only ? "yes" : "no");
}

bool GPIOManager::getWiFiDetectRelayOnlyMaster() {
    return wifi_detect_only_master_;
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
    // 避让 WiFi 扫描：扫描期间 WiFi 射频持续工作（150mA+），
    // 此时 commit 触发 flash 擦除（50-100mA/30ms）会叠加电流峰值，可能触发 BOD
    int8_t scanStatus = WiFi.scanComplete();
    if (scanStatus == WIFI_SCAN_RUNNING) return;
    // P1: 避让 WiFi boost 期：16.5dBm 时电流 200-300mA，
    // 此时 commit 会叠加电流峰值，可能触发 BOD
    if (WiFiManager::isTxPowerBoosted()) return;
    unsigned long now = millis();
    if (now - eeprom_last_commit_ < 5000) return;
    EEPROM.commit();
    eeprom_last_commit_ = now;
    eeprom_dirty_ = false;
}