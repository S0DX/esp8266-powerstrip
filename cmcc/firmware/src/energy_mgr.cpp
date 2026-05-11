#include "energy_mgr.h"
#include "config.h"
#include "sy7t609.h"

float EnergyManager::total_energy_ = 0;
float EnergyManager::last_value_ = 0;
unsigned long EnergyManager::last_save_time_ = 0;

#define EEPROM_ENERGY_ADDR   128
#define EEPROM_SIZE          256

void EnergyManager::init() {
    EEPROM.begin(EEPROM_SIZE);

    uint32_t magic = 0;
    for (int i = 0; i < 4; i++) {
        magic = (magic << 8) | EEPROM.read(EEPROM_ENERGY_ADDR + i);
    }

    if (magic == 0xDEADBEEF) {
        uint32_t stored_value = 0;
        for (int i = 0; i < 4; i++) {
            stored_value = (stored_value << 8) | EEPROM.read(EEPROM_ENERGY_ADDR + 4 + i);
        }
        memcpy(&total_energy_, &stored_value, sizeof(float));
        Serial.printf("[Energy] Loaded from flash: %.2f Wh\n", total_energy_);
    }
}

void EnergyManager::handle() {
    float current = SY7T609::getEnergy();

    if (current < last_value_) {
        float delta = current;
        total_energy_ += delta;
    } else {
        float delta = current - last_value_;
        total_energy_ += delta;
    }

    last_value_ = current;

    if (millis() - last_save_time_ > ENERGY_SAVE_INTERVAL_MS) {
        save();
        last_save_time_ = millis();
    }
}

float EnergyManager::getTotalEnergy() {
    return total_energy_;
}

float EnergyManager::getLastValue() {
    return last_value_;
}

void EnergyManager::save() {
    uint32_t magic = 0xDEADBEEF;
    uint32_t stored_value = 0;
    memcpy(&stored_value, &total_energy_, sizeof(float));

    for (int i = 3; i >= 0; i--) {
        EEPROM.write(EEPROM_ENERGY_ADDR + i, magic & 0xFF);
        magic >>= 8;
    }
    for (int i = 3; i >= 0; i--) {
        EEPROM.write(EEPROM_ENERGY_ADDR + 4 + i, stored_value & 0xFF);
        stored_value >>= 8;
    }

    EEPROM.commit();
    Serial.printf("[Energy] Saved: %.2f Wh\n", total_energy_);
}

void EnergyManager::reset() {
    total_energy_ = 0;
    last_value_ = 0;
    save();
}
