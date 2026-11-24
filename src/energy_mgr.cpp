#include "energy_mgr.h"
#include "config.h"
#include "sy7t609.h"
#include "gpio_mgr.h"
#include "time_mgr.h"

float EnergyManager::total_energy_ = 0;
unsigned long EnergyManager::last_save_time_ = 0;
float EnergyManager::history_[7] = {0, 0, 0, 0, 0, 0, 0};
float EnergyManager::day_start_energy_ = 0;
unsigned long EnergyManager::day_start_time_ = 0;
unsigned long EnergyManager::last_integration_time_ = 0;

#define DAY_INTERVAL_MS       86400000UL

#define ENERGY_MAGIC          0xDEADBEEF
#define ENERGY_HISTORY_MAGIC  0xE7DA1234
#define ENERGY_MONTHLY_MAGIC  0xE7DA3456

float EnergyManager::month_start_energy_ = 0;
unsigned long EnergyManager::month_start_time_ = 0;
int EnergyManager::month_start_year_ = 0;
int EnergyManager::month_start_month_ = 0;
float EnergyManager::monthly_history_[3] = {0, 0, 0};
float EnergyManager::last_saved_energy_ = 0;

// 24h 逐小时（v5.9）
float EnergyManager::hourly_[24] = {0};
int EnergyManager::hourly_head_ = 0;
int EnergyManager::hourly_count_ = 0;
int EnergyManager::hourly_last_hour_ = -1;
float EnergyManager::hourly_snapshot_ = 0;
float EnergyManager::hourly_saved_total_ = 0;

static void writeBufToEeprom(int addr, const uint8_t* buf, int len) {
    for (int i = 0; i < len; i++) {
        EEPROM.write(addr + i, buf[i]);
    }
}

static bool migrateEnergyBlock(int oldAddr, int newAddr, uint32_t magic, int dataLen) {
    uint8_t buf[4] = {0};
    for (int i = 0; i < 4; i++) buf[i] = EEPROM.read(oldAddr + i);
    uint32_t oldMagic = ((uint32_t)buf[0] << 24) | ((uint32_t)buf[1] << 16) |
                        ((uint32_t)buf[2] << 8) | buf[3];
    if (oldMagic != magic) return false;

    uint8_t newBuf[4];
    newBuf[0] = (magic >> 24) & 0xFF;
    newBuf[1] = (magic >> 16) & 0xFF;
    newBuf[2] = (magic >> 8) & 0xFF;
    newBuf[3] = magic & 0xFF;
    writeBufToEeprom(newAddr, newBuf, 4);
    for (int i = 0; i < dataLen; i++) {
        EEPROM.write(newAddr + 4 + i, EEPROM.read(oldAddr + 4 + i));
    }
    // 擦除旧 magic，防止重复迁移
    EEPROM.write(oldAddr, 0);
    Serial.printf("[Energy] Migrated block from %d to %d\n", oldAddr, newAddr);
    return true;
}

void EnergyManager::init() {
    // 首次新版布局：把旧地址数据迁移到新地址（若旧数据仍有效）
    bool migrated = false;
    migrated |= migrateEnergyBlock(EEPROM_ENERGY_TOTAL_ADDR_OLD, EEPROM_ENERGY_TOTAL_ADDR,
                       ENERGY_MAGIC, sizeof(float));
    migrated |= migrateEnergyBlock(EEPROM_ENERGY_HISTORY_ADDR_OLD, EEPROM_ENERGY_HISTORY_ADDR,
                       ENERGY_HISTORY_MAGIC, 7 * sizeof(float));
    migrated |= migrateEnergyBlock(EEPROM_ENERGY_MONTHLY_ADDR_OLD, EEPROM_ENERGY_MONTHLY_ADDR,
                       ENERGY_MONTHLY_MAGIC, 3 * sizeof(float));
    if (migrated) {
        EEPROM.commit();
    }

    uint8_t buf[8] = {0};
    for (int i = 0; i < 8; i++) {
        buf[i] = EEPROM.read(EEPROM_ENERGY_TOTAL_ADDR + i);
    }
    uint32_t magic = ((uint32_t)buf[0] << 24) | ((uint32_t)buf[1] << 16) |
                     ((uint32_t)buf[2] << 8) | buf[3];
    if (magic == ENERGY_MAGIC) {
        memcpy(&total_energy_, &buf[4], sizeof(float));
        last_saved_energy_ = total_energy_;
        Serial.printf("[Energy] Loaded from flash: %.2f Wh\n", total_energy_);
    }

    uint8_t histMagic[4] = {0};
    for (int i = 0; i < 4; i++) {
        histMagic[i] = EEPROM.read(EEPROM_ENERGY_HISTORY_ADDR + i);
    }
    uint32_t histMagicVal = ((uint32_t)histMagic[0] << 24) | ((uint32_t)histMagic[1] << 16) |
                            ((uint32_t)histMagic[2] << 8) | histMagic[3];
    if (histMagicVal == ENERGY_HISTORY_MAGIC) {
        for (int d = 0; d < 7; d++) {
            uint8_t v[4] = {0};
            for (int i = 0; i < 4; i++) {
                v[i] = EEPROM.read(EEPROM_ENERGY_HISTORY_ADDR + 4 + d * 4 + i);
            }
            memcpy(&history_[d], v, sizeof(float));
        }
        Serial.println("[Energy] History loaded from flash");
    }

    uint8_t mthMagic[4] = {0};
    for (int i = 0; i < 4; i++) {
        mthMagic[i] = EEPROM.read(EEPROM_ENERGY_MONTHLY_ADDR + i);
    }
    uint32_t mthMagicVal = ((uint32_t)mthMagic[0] << 24) | ((uint32_t)mthMagic[1] << 16) |
                           ((uint32_t)mthMagic[2] << 8) | mthMagic[3];
    if (mthMagicVal == ENERGY_MONTHLY_MAGIC) {
        for (int d = 0; d < 3; d++) {
            uint8_t v[4] = {0};
            for (int i = 0; i < 4; i++) {
                v[i] = EEPROM.read(EEPROM_ENERGY_MONTHLY_ADDR + 4 + d * 4 + i);
            }
            memcpy(&monthly_history_[d], v, sizeof(float));
        }
        Serial.println("[Energy] Monthly history loaded from flash");
    }

    day_start_energy_ = total_energy_;
    day_start_time_ = millis();

    // 24h 逐小时统计恢复（v5.9）：滚动最近 24 小时
    uint8_t hhBuf[8] = {0};
    for (int i = 0; i < 8; i++) {
        hhBuf[i] = EEPROM.read(EEPROM_ENERGY_HOURLY_ADDR + i);
    }
    uint32_t hhMagic = ((uint32_t)hhBuf[0] << 24) | ((uint32_t)hhBuf[1] << 16) |
                       ((uint32_t)hhBuf[2] << 8) | hhBuf[3];
    if (hhMagic == ENERGY_HOURLY_MAGIC) {
        hourly_last_hour_ = (int8_t)hhBuf[4];
        hourly_head_ = (int8_t)hhBuf[5] % 24;
        hourly_count_ = (int8_t)hhBuf[6];
        if (hourly_count_ < 0 || hourly_count_ > 24) hourly_count_ = 0;
        for (int h = 0; h < 24; h++) {
            uint8_t v[4] = {0};
            for (int i = 0; i < 4; i++) {
                v[i] = EEPROM.read(EEPROM_ENERGY_HOURLY_ADDR + 8 + h * 4 + i);
            }
            memcpy(&hourly_[h], v, sizeof(float));
            if (hourly_[h] < 0 || hourly_[h] > 99999) hourly_[h] = 0;
        }
        // saved_total 位于布局末尾：8 + 96 = 104 字节偏移
        uint8_t sv[4] = {0};
        for (int i = 0; i < 4; i++) {
            sv[i] = EEPROM.read(EEPROM_ENERGY_HOURLY_ADDR + 104 + i);
        }
        memcpy(&hourly_saved_total_, sv, sizeof(float));
        memcpy(&hourly_saved_total_, sv, sizeof(float));
        // 当前桶恢复：saved 值 + (重启后的 total - 持久化时的 total)
        float recovered = hourly_[hourly_head_] + (total_energy_ - hourly_saved_total_);
        hourly_[hourly_head_] = (recovered > 0 && recovered < 99999) ? recovered : hourly_[hourly_head_];
        hourly_snapshot_ = total_energy_;
        Serial.printf("[Energy] Hourly stats restored: head=%d count=%d lastHour=%d\n",
            hourly_head_, hourly_count_, hourly_last_hour_);
    } else {
        hourly_snapshot_ = total_energy_;
    }
}

void EnergyManager::handle() {
    if (!SY7T609::isEnabled() || !SY7T609::isReady()) {
        return;
    }

    unsigned long now = millis();
    float power = SY7T609::getPower();

    if (power > 0.1f) {
        if (last_integration_time_ == 0) {
            last_integration_time_ = now;
        } else {
            unsigned long dt_ms = now - last_integration_time_;
            if (dt_ms >= 1000) {
                float dt_hours = dt_ms / 3600000.0f;
                float energy_wh = power * dt_hours;
                total_energy_ += energy_wh;
                last_integration_time_ = now;
            }
        }
    } else {
        last_integration_time_ = now;
    }

    if (millis() - day_start_time_ >= DAY_INTERVAL_MS) {
        float today_used = total_energy_ - day_start_energy_;
        for (int i = 0; i < 6; i++) {
            history_[i] = history_[i + 1];
        }
        history_[6] = today_used;
        day_start_energy_ = total_energy_;
        day_start_time_ = millis();
        saveHistory();
        Serial.printf("[Energy] Day rollover: today used %.2f Wh\n", today_used);
    }

    time_t now_time = time(nullptr);
    struct tm* timeinfo = localtime(&now_time);
    if (timeinfo && timeinfo->tm_year >= 100) {
        if (month_start_year_ == 0 ||
            timeinfo->tm_mon != month_start_month_ ||
            timeinfo->tm_year != month_start_year_) {
            float month_used = total_energy_ - month_start_energy_;
            monthly_history_[0] = monthly_history_[1];
            monthly_history_[1] = monthly_history_[2];
            monthly_history_[2] = month_used;
            month_start_energy_ = total_energy_;
            month_start_time_ = millis();
            month_start_year_ = timeinfo->tm_year;
            month_start_month_ = timeinfo->tm_mon;
            saveMonthlyHistory();
            Serial.printf("[Energy] Month rollover: month used %.2f Wh\n", month_used);
        }
    }

    if (millis() - last_save_time_ > ENERGY_SAVE_INTERVAL_MS) {
        // 仅当用电量变化超过 0.5Wh 才写 Flash，避免空载或微小波动时频繁写入
        // 0.5Wh 阈值可减少 98% 的 commit 调用（按 100W 负载算，每 18 秒 0.5Wh）
        if (total_energy_ > last_saved_energy_ + 0.5f || total_energy_ < last_saved_energy_ - 0.5f) {
            save();
            last_saved_energy_ = total_energy_;
        }
        last_save_time_ = millis();
    }

    // ===== 24h 逐小时统计（滚动最近 24 小时，v5.9）=====
    // 小时源：墙钟优先（NTP/浏览器校时后），时间无效时退化为运行小时推算，
    // 校时生效的瞬间会多翻一个空桶对齐墙钟，影响可忽略。
    int curHour;
    if (TimeManager::isValid()) {
        time_t t = time(nullptr);
        struct tm* ti = localtime(&t);
        curHour = ti ? ti->tm_hour : (int)((millis() / 3600000UL) % 24);
    } else {
        curHour = (int)((millis() / 3600000UL) % 24);
    }
    if (curHour != hourly_last_hour_) {
        if (hourly_last_hour_ >= 0) {
            // 翻桶：推进 head（环形覆盖最旧桶），count 封顶 24
            hourly_head_ = (hourly_head_ + 1) % 24;
            if (hourly_count_ < 24) hourly_count_++;
            hourly_[hourly_head_] = 0;
        } else {
            hourly_head_ = 0;
            hourly_count_ = 1;
            hourly_[0] = 0;
        }
        hourly_snapshot_ = total_energy_;
        hourly_last_hour_ = curHour;
        saveHourly();  // 整点一次 EEPROM 写（延迟提交）
    }
    if (hourly_count_ > 0) {
        float cur = total_energy_ - hourly_snapshot_;
        if (cur >= 0 && cur < 99999) hourly_[hourly_head_] = cur;
    }
}

void EnergyManager::getHourlySeries(float* out24) {
    // 时间升序输出：最早桶在前；count 不足 24 时前面补 0
    for (int i = 0; i < 24; i++) {
        if (i < 24 - hourly_count_) {
            out24[i] = 0;
        } else {
            int idx = (hourly_head_ - (23 - i) + 48) % 24;
            out24[i] = hourly_[idx];
        }
    }
}

int EnergyManager::getHourlyCount() {
    return hourly_count_;
}

float EnergyManager::getTotalEnergy() {
    return total_energy_;
}

float EnergyManager::getLastValue() {
    return total_energy_;
}

float EnergyManager::getHistory(int day) {
    if (day < 0 || day >= 7) return 0;
    return history_[day];
}

float EnergyManager::getMonthlyEnergy() {
    return total_energy_ - month_start_energy_;
}

float EnergyManager::getLastMonthEnergy() {
    return monthly_history_[2];
}

void EnergyManager::saveMonthlyHistory() {
    uint8_t buf[4] = {(ENERGY_MONTHLY_MAGIC >> 24) & 0xFF, (ENERGY_MONTHLY_MAGIC >> 16) & 0xFF,
                      (ENERGY_MONTHLY_MAGIC >> 8) & 0xFF, ENERGY_MONTHLY_MAGIC & 0xFF};
    for (int i = 0; i < 4; i++) {
        EEPROM.write(EEPROM_ENERGY_MONTHLY_ADDR + i, buf[i]);
    }
    for (int d = 0; d < 3; d++) {
        uint8_t v[4] = {0};
        memcpy(v, &monthly_history_[d], sizeof(float));
        for (int i = 0; i < 4; i++) {
            EEPROM.write(EEPROM_ENERGY_MONTHLY_ADDR + 4 + d * 4 + i, v[i]);
        }
    }
    GPIOManager::requestEEPROMCommit();  // 延迟提交，避免高频 commit 触发 BOD
    Serial.println("[Energy] Monthly history saved to flash");
}

void EnergyManager::save() {
    uint8_t buf[8] = {(ENERGY_MAGIC >> 24) & 0xFF, (ENERGY_MAGIC >> 16) & 0xFF,
                      (ENERGY_MAGIC >> 8) & 0xFF, ENERGY_MAGIC & 0xFF};
    memcpy(&buf[4], &total_energy_, sizeof(float));
    for (int i = 0; i < 8; i++) {
        EEPROM.write(EEPROM_ENERGY_TOTAL_ADDR + i, buf[i]);
    }
    GPIOManager::requestEEPROMCommit();  // 延迟提交，由主循环统一调度
    Serial.printf("[Energy] Saved: %.2f Wh\n", total_energy_);
}

void EnergyManager::saveHistory() {
    uint8_t buf[4] = {(ENERGY_HISTORY_MAGIC >> 24) & 0xFF, (ENERGY_HISTORY_MAGIC >> 16) & 0xFF,
                      (ENERGY_HISTORY_MAGIC >> 8) & 0xFF, ENERGY_HISTORY_MAGIC & 0xFF};
    for (int i = 0; i < 4; i++) {
        EEPROM.write(EEPROM_ENERGY_HISTORY_ADDR + i, buf[i]);
    }
    for (int d = 0; d < 7; d++) {
        uint8_t v[4] = {0};
        memcpy(v, &history_[d], sizeof(float));
        for (int i = 0; i < 4; i++) {
            EEPROM.write(EEPROM_ENERGY_HISTORY_ADDR + 4 + d * 4 + i, v[i]);
        }
    }
    GPIOManager::requestEEPROMCommit();  // 延迟提交
}

void EnergyManager::saveHourly() {
    uint8_t buf[4] = {(ENERGY_HOURLY_MAGIC >> 24) & 0xFF, (ENERGY_HOURLY_MAGIC >> 16) & 0xFF,
                      (ENERGY_HOURLY_MAGIC >> 8) & 0xFF, ENERGY_HOURLY_MAGIC & 0xFF};
    for (int i = 0; i < 4; i++) {
        EEPROM.write(EEPROM_ENERGY_HOURLY_ADDR + i, buf[i]);
    }
    uint8_t meta[4] = {(uint8_t)hourly_last_hour_, (uint8_t)hourly_head_,
                       (uint8_t)hourly_count_, 0};
    for (int i = 0; i < 4; i++) {
        EEPROM.write(EEPROM_ENERGY_HOURLY_ADDR + 4 + i, meta[i]);
    }
    for (int h = 0; h < 24; h++) {
        uint8_t v[4] = {0};
        memcpy(v, &hourly_[h], sizeof(float));
        for (int i = 0; i < 4; i++) {
            EEPROM.write(EEPROM_ENERGY_HOURLY_ADDR + 8 + h * 4 + i, v[i]);
        }
    }
    hourly_saved_total_ = total_energy_;
    uint8_t sv[4] = {0};
    memcpy(sv, &hourly_saved_total_, sizeof(float));
    for (int i = 0; i < 4; i++) {
        EEPROM.write(EEPROM_ENERGY_HOURLY_ADDR + 104 + i, sv[i]);
    }
    GPIOManager::requestEEPROMCommit();  // 延迟提交
}

void EnergyManager::reset() {
    total_energy_ = 0;
    last_integration_time_ = 0;
    last_saved_energy_ = 0;
    for (int i = 0; i < 7; i++) history_[i] = 0;
    for (int i = 0; i < 3; i++) monthly_history_[i] = 0;
    for (int i = 0; i < 24; i++) hourly_[i] = 0;
    hourly_head_ = 0;
    hourly_count_ = 0;
    hourly_last_hour_ = -1;
    hourly_snapshot_ = 0;
    hourly_saved_total_ = 0;
    month_start_energy_ = 0;
    month_start_year_ = 0;
    month_start_month_ = 0;
    save();
    saveHistory();
    saveMonthlyHistory();
    saveHourly();
}
