#include "sy7t609.h"
#include "config.h"

HardwareSerial* SY7T609::serial_ = nullptr;
bool SY7T609::ready_ = false;
bool SY7T609::enabled_ = true;
bool SY7T609::flash_mode_ = false;
bool SY7T609::gpio0_checked_ = false;
unsigned long SY7T609::start_time_ = 0;
float SY7T609::voltage_ = 0.0f;
float SY7T609::current_ = 0.0f;
float SY7T609::power_ = 0.0f;
float SY7T609::power_factor_ = 0.0f;
float SY7T609::frequency_ = 0.0f;
float SY7T609::temperature_ = 0.0f;
float SY7T609::energy_ = 0.0f;
unsigned long SY7T609::last_read_ = 0;
bool SY7T609::auto_report_ = false;
unsigned long SY7T609::last_init_attempt_ = 0;
int SY7T609::init_retry_count_ = 0;
String SY7T609::debug_log_ = "";
int SY7T609::debug_log_count_ = 0;
uint8_t SY7T609::read_state_ = 0;

float SY7T609::voltage_scale_ = 1000.0f;
float SY7T609::current_scale_ = 1000.0f;
float SY7T609::power_scale_ = 1000.0f;
float SY7T609::pf_scale_ = 1000.0f;
float SY7T609::frequency_scale_ = 1000.0f;
float SY7T609::temperature_scale_ = 1000.0f;

bool g_meter_enabled = false;

#define SY7T609_READ_INTERVAL 1000
#define SY7T609_STARTUP_DELAY_MS 2000
#define SY7T609_INIT_RETRY_MAX 3
#define SY7T609_INIT_RETRY_DELAY_MS 500
#define EEPROM_SY7T609_ENABLED_ADDR 210
#define EEPROM_SY7T609_FLASH_MODE_ADDR 211
#define EEPROM_SIZE 256
#define GPIO0_CHECK_TIMEOUT_MS 100

#define ADDR_PF        0x0048
#define ADDR_VRMS      0x0033
#define ADDR_IRMS      0x0036
#define ADDR_POWER     0x0039
#define ADDR_VAR       0x003C
#define ADDR_FREQUENCY 0x0042
#define ADDR_CTEMP     0x0027
#define ADDR_COMMAND   0x0000

#define CMD_REG_SOFT_RESET   0xBD0000
#define CMD_REG_CLEAR_ENERGY 0xEC0000

#define SSI_HEADER        0xAA
#define CMD_SELECT_ADDR   0xA3
#define CMD_READ_REG      0xE3
#define CMD_WRITE_REG     0xD3
#define REPLY_ACK         0xAA
#define REPLY_ACK_NO_DATA 0xAD

bool SY7T609::checkGPIO0ForFlashMode() {
    if (gpio0_checked_) {
        return flash_mode_;
    }

    pinMode(RELAY_MASTER_PIN, INPUT_PULLUP);
    delayMicroseconds(50);

    bool isLow = (digitalRead(RELAY_MASTER_PIN) == LOW);

    pinMode(RELAY_MASTER_PIN, OUTPUT);
    digitalWrite(RELAY_MASTER_PIN, HIGH);

    gpio0_checked_ = true;

    if (isLow) {
        flash_mode_ = true;
        enabled_ = false;
        g_meter_enabled = false;

        EEPROM.write(EEPROM_SY7T609_FLASH_MODE_ADDR, 0xAA);
        EEPROM.commit();

        Serial.println("[SY7T609] Flash boot mode detected (GPIO0=LOW), Serial reserved for flashing");
    }

    return flash_mode_;
}

bool SY7T609::canUseSerial() {
    if (flash_mode_) return false;
    if (!enabled_) return false;
    if (!gpio0_checked_) return false;
    if (!serial_) return false;
    return true;
}

uint8_t SY7T609::calculateChecksum(const uint8_t* data, size_t size) {
    uint8_t sum = 0;
    for (size_t i = 0; i < size - 1; i++) {
        sum += data[i];
    }
    return ~sum + 1;
}

bool SY7T609::sendCommand(uint16_t addr, uint32_t value) {
    if (!canUseSerial()) {
        return false;
    }

    uint8_t tx_data[10];
    tx_data[0] = SSI_HEADER;
    tx_data[1] = 0x0A;
    tx_data[2] = CMD_SELECT_ADDR;
    tx_data[3] = addr & 0xFF;
    tx_data[4] = (addr >> 8) & 0xFF;
    tx_data[5] = CMD_WRITE_REG;
    tx_data[6] = value & 0xFF;
    tx_data[7] = (value >> 8) & 0xFF;
    tx_data[8] = (value >> 16) & 0xFF;
    tx_data[9] = calculateChecksum(tx_data, 10);

    serial_->flush();
    serial_->write(tx_data, 10);

    unsigned long start = millis();
    while (millis() - start < 100) {
        if (serial_->available() > 0) {
            uint8_t reply = serial_->read();
            if (reply == REPLY_ACK_NO_DATA || reply == REPLY_ACK) {
                return true;
            }
        }
        yield();
    }
    return false;
}

bool SY7T609::readRegister(uint16_t addr, uint32_t* value) {
    if (!canUseSerial()) {
        return false;
    }

    uint8_t tx_data[7];
    tx_data[0] = SSI_HEADER;
    tx_data[1] = 0x07;
    tx_data[2] = CMD_SELECT_ADDR;
    tx_data[3] = addr & 0xFF;
    tx_data[4] = (addr >> 8) & 0xFF;
    tx_data[5] = CMD_READ_REG;
    tx_data[6] = calculateChecksum(tx_data, 7);

    for (uint8_t retry = 0; retry < 2; retry++) {
        while (serial_->available()) {
            serial_->read();
        }

        serial_->flush();
        serial_->write(tx_data, 7);

        unsigned long start = millis();
        while (millis() - start < 50) {
            if (serial_->available() >= 6) {
                uint8_t rx_data[6];
                for (int i = 0; i < 6; i++) {
                    rx_data[i] = serial_->read();
                }

                if (rx_data[0] == REPLY_ACK) {
                    uint8_t checksum = calculateChecksum(rx_data, 6);
                    if (checksum == rx_data[5]) {
                        *value = ((uint32_t)rx_data[4] << 16) | ((uint32_t)rx_data[3] << 8) | rx_data[2];

                        if (debug_log_count_ < 20) {
                            char log_buf[128];
                            snprintf(log_buf, sizeof(log_buf), "[UART] addr=0x%04X value=0x%06X", addr, *value);
                            debug_log_ += String(log_buf) + "\n";
                            debug_log_count_++;
                        }

                        return true;
                    }
                }
            }
            yield();
        }
        if (retry < 1) {
            delayMicroseconds(500);
        }
    }
    return false;
}

bool SY7T609::initializeSY7T609() {
    if (init_retry_count_ >= SY7T609_INIT_RETRY_MAX) {
        Serial.println("[SY7T609] Max initialization retries reached, giving up");
        return false;
    }

    if (!serial_) {
        Serial.end();
        Serial.begin(SY7T609_BAUD_RATE, SERIAL_8N1);
        Serial1.begin(115200);
        g_meter_enabled = true;
        serial_ = &Serial;
        delay(100);
    }

    while (serial_->available()) {
        serial_->read();
    }

    sendCommand(ADDR_COMMAND, CMD_REG_SOFT_RESET);
    delay(100);

    uint32_t version = 0;
    if (readRegister(0x0003, &version) && version != 0 && version != 0xFFFFFF) {
        Serial.printf("[SY7T609] Chip connected, firmware: 0x%06X\n", version);
        init_retry_count_ = 0;
        return true;
    } else {
        Serial.println("[SY7T609] Chip not responding, will retry...");
        init_retry_count_++;
        if (serial_) {
            serial_ = nullptr;
        }
        delay(SY7T609_INIT_RETRY_DELAY_MS);
        return false;
    }
}

void SY7T609::init() {
    start_time_ = millis();
    last_init_attempt_ = 0;
    init_retry_count_ = 0;

    if (EEPROM.read(EEPROM_SY7T609_FLASH_MODE_ADDR) == 0xAA) {
        flash_mode_ = true;
        gpio0_checked_ = true;
        enabled_ = false;
        g_meter_enabled = false;
        Serial.println("[SY7T609] Flash mode flag found in EEPROM, disabled");
        return;
    }

    checkGPIO0ForFlashMode();

    if (flash_mode_) {
        return;
    }

    loadFromEEPROM();
    g_meter_enabled = enabled_;

    if (enabled_) {
        initializeSY7T609();
    } else {
        Serial.println("[SY7T609] Disabled (Serial reserved for flashing/debugging)");
    }
}

void SY7T609::handle() {
    if (!canUseSerial()) return;

    if (millis() - last_read_ < SY7T609_READ_INTERVAL) return;
    last_read_ = millis();

    uint32_t value = 0;

    switch (read_state_) {
        case 0:
            if (readRegister(ADDR_PF, &value)) {
                if (value >= 0x800000) value = 0x01000000 - value;
                power_factor_ = value / pf_scale_;
                read_state_ = 1;
            }
            break;
        case 1:
            if (readRegister(ADDR_VRMS, &value)) {
                voltage_ = value / voltage_scale_;
                if (debug_log_count_ < 20) {
                    char log_buf[128];
                    snprintf(log_buf, sizeof(log_buf), "[SY7T609] VRMS raw=%u scale=%.1f result=%.2fV", value, voltage_scale_, voltage_);
                    debug_log_ += String(log_buf) + "\n";
                    debug_log_count_++;
                }
                read_state_ = 2;
            }
            break;
        case 2:
            if (readRegister(ADDR_IRMS, &value)) {
                current_ = value / current_scale_;
                if (debug_log_count_ < 20) {
                    char log_buf[128];
                    snprintf(log_buf, sizeof(log_buf), "[SY7T609] IRMS raw=%u scale=%.1f result=%.4fA", value, current_scale_, current_);
                    debug_log_ += String(log_buf) + "\n";
                    debug_log_count_++;
                }
                read_state_ = 3;
            }
            break;
        case 3:
            if (readRegister(ADDR_POWER, &value)) {
                if (value >= 0x800000) value = 0x01000000 - value;
                power_ = value / power_scale_;
                if (debug_log_count_ < 20) {
                    char log_buf[128];
                    snprintf(log_buf, sizeof(log_buf), "[SY7T609] POWER raw=%u scale=%.1f result=%.2fW", value, power_scale_, power_);
                    debug_log_ += String(log_buf) + "\n";
                    debug_log_count_++;
                }
                read_state_ = 4;
            }
            break;
        case 4:
            if (readRegister(ADDR_VAR, &value)) {
                if (value >= 0x800000) value = 0x01000000 - value;
                read_state_ = 5;
            }
            break;
        case 5:
            read_state_ = 6;
            break;
        case 6:
            if (readRegister(ADDR_FREQUENCY, &value)) {
                frequency_ = value / frequency_scale_;
                read_state_ = 7;
            }
            break;
        case 7:
            if (readRegister(ADDR_CTEMP, &value)) {
                temperature_ = value / temperature_scale_;
                read_state_ = 0;
                ready_ = true;
            }
            break;
    }
}

bool SY7T609::isReady() {
    return ready_;
}

bool SY7T609::isEnabled() {
    return enabled_;
}

bool SY7T609::isFlashMode() {
    return flash_mode_;
}

void SY7T609::setEnabled(bool enabled) {
    if (flash_mode_) {
        Serial.println("[SY7T609] Cannot enable in flash mode!");
        return;
    }

    enabled_ = enabled;
    g_meter_enabled = enabled;
    auto_report_ = false;
    init_retry_count_ = 0;
    saveToEEPROM();

    if (enabled) {
        initializeSY7T609();
    } else {
        serial_ = nullptr;
        Serial1.end();
        Serial.end();
        Serial.begin(115200);
        ready_ = false;
        Serial.println("[SY7T609] Disabled, debug back to Serial0");
    }
}

void SY7T609::loadFromEEPROM() {
    enabled_ = EEPROM.read(EEPROM_SY7T609_ENABLED_ADDR) == 1;
    Serial.printf("[SY7T609] Loaded from EEPROM: %s\n", enabled_ ? "enabled" : "disabled");
}

void SY7T609::saveToEEPROM() {
    EEPROM.write(EEPROM_SY7T609_ENABLED_ADDR, enabled_ ? 1 : 0);
    EEPROM.commit();
    Serial.printf("[SY7T609] Saved to EEPROM: %s\n", enabled_ ? "enabled" : "disabled");
}

float SY7T609::getVoltage() { return voltage_; }
float SY7T609::getCurrent() { return current_; }
float SY7T609::getPower() { return power_; }
float SY7T609::getPowerFactor() { return power_factor_; }
float SY7T609::getFrequency() { return frequency_; }
float SY7T609::getTemperature() { return temperature_; }
float SY7T609::getEnergy() { return energy_; }

void SY7T609::resetEnergy() {
    energy_ = 0.0f;
    if (enabled_) {
        sendCommand(ADDR_COMMAND, CMD_REG_CLEAR_ENERGY);
    }
}

String SY7T609::getDebugLog() {
    return debug_log_;
}

void SY7T609::clearDebugLog() {
    debug_log_ = "";
    debug_log_count_ = 0;
}
