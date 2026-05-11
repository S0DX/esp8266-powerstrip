#include "energy_mgr.h"
#include "config.h"
#include "sy7t609.h"

float EnergyManager::total_energy_ = 0;
unsigned long EnergyManager::last_save_time_ = 0;
float EnergyManager::history_[7] = {0, 0, 0, 0, 0, 0, 0};
float EnergyManager::day_start_energy_ = 0;
unsigned long EnergyManager::day_start_time_ = 0;
unsigned long EnergyManager::last_integration_time_ = 0;

#define EEPROM_ENERGY_ADDR    128
#define EEPROM_HISTORY_ADDR   32
#define EEPROM_MONTHLY_ADDR   300
#define DAY_INTERVAL_MS       86400000UL

float EnergyManager::month_start_energy_ = 0;
unsigned long EnergyManager::month_start_time_ = 0;
int EnergyManager::month_start_year_ = 0;
int EnergyManager::month_start_month_ = 0;
float EnergyManager::monthly_history_[3] = {0, 0, 0};

void EnergyManager::init() {
    uint8_t buf[8] = {0};
    for (int i = 0; i < 8; i++) {
        buf[i] = EEPROM.read(EEPROM_ENERGY_ADDR + i);
    }
    if (buf[0] == 0xDE && buf[1] == 0xAD && buf[2] == 0xBE && buf[3] == 0xEF) {
        memcpy(&total_energy_, &buf[4], sizeof(float));
        Serial.printf("[Energy] Loaded from flash: %.2f Wh\n", total_energy_);
    }

    uint8_t histMagic[4] = {0};
    for (int i = 0; i < 4; i++) {
        histMagic[i] = EEPROM.read(EEPROM_HISTORY_ADDR + i);
    }
    if (histMagic[0] == 0xE7 && histMagic[1] == 0xDA && histMagic[2] == 0x12 && histMagic[3] == 0x34) {
        for (int d = 0; d < 7; d++) {
            uint8_t v[4] = {0};
            for (int i = 0; i < 4; i++) {
                v[i] = EEPROM.read(EEPROM_HISTORY_ADDR + 4 + d * 4 + i);
            }
            memcpy(&history_[d], v, sizeof(float));
        }
        Serial.println("[Energy] History loaded from flash");
    }

    uint8_t mthMagic[4] = {0};
    for (int i = 0; i < 4; i++) {
        mthMagic[i] = EEPROM.read(EEPROM_MONTHLY_ADDR + i);
    }
    if (mthMagic[0] == 0xE7 && mthMagic[1] == 0xDA && mthMagic[2] == 0x34 && mthMagic[3] == 0x56) {
        for (int d = 0; d < 3; d++) {
            uint8_t v[4] = {0};
            for (int i = 0; i < 4; i++) {
                v[i] = EEPROM.read(EEPROM_MONTHLY_ADDR + 4 + d * 4 + i);
            }
            memcpy(&monthly_history_[d], v, sizeof(float));
        }
        Serial.println("[Energy] Monthly history loaded from flash");
    }

    day_start_energy_ = total_energy_;
    day_start_time_ = millis();
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
        save();
        last_save_time_ = millis();
    }
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
    uint8_t buf[4] = {0xE7, 0xDA, 0x34, 0x56};
    for (int i = 0; i < 4; i++) {
        EEPROM.write(EEPROM_MONTHLY_ADDR + i, buf[i]);
    }
    for (int d = 0; d < 3; d++) {
        uint8_t v[4] = {0};
        memcpy(v, &monthly_history_[d], sizeof(float));
        for (int i = 0; i < 4; i++) {
            EEPROM.write(EEPROM_MONTHLY_ADDR + 4 + d * 4 + i, v[i]);
        }
    }
    EEPROM.commit();
    Serial.println("[Energy] Monthly history saved to flash");
}

void EnergyManager::save() {
    uint8_t buf[8] = {0xDE, 0xAD, 0xBE, 0xEF};
    memcpy(&buf[4], &total_energy_, sizeof(float));
    for (int i = 0; i < 8; i++) {
        EEPROM.write(EEPROM_ENERGY_ADDR + i, buf[i]);
    }
    EEPROM.commit();
    Serial.printf("[Energy] Saved: %.2f Wh\n", total_energy_);
}

void EnergyManager::saveHistory() {
    uint8_t buf[4] = {0xE7, 0xDA, 0x12, 0x34};
    for (int i = 0; i < 4; i++) {
        EEPROM.write(EEPROM_HISTORY_ADDR + i, buf[i]);
    }
    for (int d = 0; d < 7; d++) {
        uint8_t v[4] = {0};
        memcpy(v, &history_[d], sizeof(float));
        for (int i = 0; i < 4; i++) {
            EEPROM.write(EEPROM_HISTORY_ADDR + 4 + d * 4 + i, v[i]);
        }
    }
    EEPROM.commit();
}

void EnergyManager::reset() {
    total_energy_ = 0;
    last_integration_time_ = 0;
    for (int i = 0; i < 7; i++) history_[i] = 0;
    for (int i = 0; i < 3; i++) monthly_history_[i] = 0;
    month_start_energy_ = 0;
    month_start_year_ = 0;
    month_start_month_ = 0;
    save();
    saveHistory();
    saveMonthlyHistory();
}
