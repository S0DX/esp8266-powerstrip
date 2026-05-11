#include "sy7t609.h"
#include "config.h"

SoftwareSerial* SY7T609::serial_ = nullptr;
bool SY7T609::ready_ = false;
unsigned long SY7T609::last_read_ = 0;
uint8_t SY7T609::state_ = 0;

float SY7T609::voltage_ = 0;
float SY7T609::current_ = 0;
float SY7T609::power_ = 0;
float SY7T609::reactive_power_ = 0;
float SY7T609::power_factor_ = 0;
float SY7T609::frequency_ = 0;
float SY7T609::temperature_ = 0;
float SY7T609::energy_ = 0;

#define SSI_HEADER             0xAA
#define CMD_SELECT_ADDR        0xA3
#define CMD_READ_REG           0xE3
#define CMD_WRITE_REG          0xD3

#define ADDR_PF                0x0048
#define ADDR_VRMS              0x0033
#define ADDR_IRMS              0x0036
#define ADDR_POWER             0x0039
#define ADDR_VAR               0x003C
#define ADDR_EPPCNT            0x0069
#define ADDR_FREQUENCY         0x0042
#define ADDR_CTEMP             0x0027
#define ADDR_COMMAND           0x0000

#define CMD_REG_SOFT_RESET     0xBD0000
#define CMD_REG_CLEAR_ENERGY   0xEC0000
#define CMD_REG_SAVE_FLASH     0xACC200
#define CMD_REG_CALIB_VOLTAGE  0xCA0020
#define CMD_REG_CALIB_CURRENT  0xCA0010

#define REPLY_ACK              0xAA
#define REPLY_ACK_NO_DATA      0xAD

void SY7T609::init(int8_t rxPin, int8_t txPin) {
    serial_ = new SoftwareSerial(rxPin, txPin);
    serial_->begin(9600);

    while (serial_->available()) {
        serial_->read();
    }

    reset();
}

void SY7T609::reset() {
    sendCommand(ADDR_COMMAND, CMD_REG_SOFT_RESET);
    delay(100);
    state_ = 0;
    last_read_ = 0;
    ready_ = false;
    voltage_ = 0;
    current_ = 0;
    power_ = 0;
    reactive_power_ = 0;
    power_factor_ = 0;
    frequency_ = 0;
    temperature_ = 0;
    energy_ = 0;
}

bool SY7T609::sendCommand(uint16_t addr, uint32_t value) {
    uint8_t tx_data[10];
    tx_data[0] = SSI_HEADER;
    tx_data[1] = 0x07;
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
    }
    return false;
}

bool SY7T609::readRegister(uint16_t addr, uint32_t* value) {
    uint8_t tx_data[7];
    tx_data[0] = SSI_HEADER;
    tx_data[1] = 0x04;
    tx_data[2] = CMD_SELECT_ADDR;
    tx_data[3] = addr & 0xFF;
    tx_data[4] = (addr >> 8) & 0xFF;
    tx_data[5] = CMD_READ_REG;
    tx_data[6] = calculateChecksum(tx_data, 7);

    serial_->flush();
    serial_->write(tx_data, 7);

    unsigned long start = millis();
    while (millis() - start < 200) {
        if (serial_->available() >= 6) {
            uint8_t rx_data[6];
            for (int i = 0; i < 6; i++) {
                rx_data[i] = serial_->read();
            }

            if (rx_data[0] == REPLY_ACK) {
                uint8_t checksum = calculateChecksum(rx_data, 6);
                if (checksum == rx_data[5]) {
                    *value = ((uint32_t)rx_data[4] << 16) | ((uint32_t)rx_data[3] << 8) | rx_data[2];
                    return true;
                }
            }
        }
    }
    return false;
}

uint8_t SY7T609::calculateChecksum(const uint8_t* data, size_t size) {
    uint8_t sum = 0;
    for (size_t i = 0; i < size - 1; i++) {
        sum += data[i];
    }
    return ~sum + 1;
}

void SY7T609::handle() {
    if (millis() - last_read_ < 1000) return;

    uint32_t value = 0;

    switch (state_) {
        case 0:
            if (readRegister(ADDR_PF, &value)) {
                if (value >= 0x800000) value = 0x01000000 - value;
                power_factor_ = value / 1000.0f;
                state_ = 1;
            }
            break;
        case 1:
            if (readRegister(ADDR_VRMS, &value)) {
                voltage_ = value / 1000.0f;
                state_ = 2;
            }
            break;
        case 2:
            if (readRegister(ADDR_IRMS, &value)) {
                current_ = value / 1000.0f;
                state_ = 3;
            }
            break;
        case 3:
            if (readRegister(ADDR_POWER, &value)) {
                if (value >= 0x800000) value = 0x01000000 - value;
                power_ = value / 1000.0f;
                state_ = 4;
            }
            break;
        case 4:
            if (readRegister(ADDR_VAR, &value)) {
                if (value >= 0x800000) value = 0x01000000 - value;
                reactive_power_ = value / 1000.0f;
                state_ = 5;
            }
            break;
        case 5:
            if (readRegister(ADDR_EPPCNT, &value)) {
                energy_ = value / 1.0f;
                state_ = 6;
            }
            break;
        case 6:
            if (readRegister(ADDR_FREQUENCY, &value)) {
                frequency_ = value / 1000.0f;
                state_ = 7;
            }
            break;
        case 7:
            if (readRegister(ADDR_CTEMP, &value)) {
                temperature_ = value / 1000.0f;
                state_ = 0;
                ready_ = true;
            }
            break;
    }

    last_read_ = millis();
}

bool SY7T609::isReady() {
    return ready_;
}

float SY7T609::getVoltage() { return voltage_; }
float SY7T609::getCurrent() { return current_; }
float SY7T609::getPower() { return power_; }
float SY7T609::getReactivePower() { return reactive_power_; }
float SY7T609::getPowerFactor() { return power_factor_; }
float SY7T609::getFrequency() { return frequency_; }
float SY7T609::getTemperature() { return temperature_; }
float SY7T609::getEnergy() { return energy_; }

void SY7T609::resetEnergy() {
    sendCommand(ADDR_COMMAND, CMD_REG_CLEAR_ENERGY);
}

void SY7T609::calibrate(uint32_t voltage_target) {
}
