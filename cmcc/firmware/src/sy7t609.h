#ifndef SY7T609_H
#define SY7T609_H

#include <Arduino.h>
#include <SoftwareSerial.h>

class SY7T609 {
public:
    static void init(int8_t rxPin, int8_t txPin);
    static void handle();
    static bool isReady();

    static float getVoltage();
    static float getCurrent();
    static float getPower();
    static float getReactivePower();
    static float getPowerFactor();
    static float getFrequency();
    static float getTemperature();
    static float getEnergy();
    static void resetEnergy();
    static void calibrate(uint32_t voltage_target);
    static void reset();

private:
    static SoftwareSerial* serial_;
    static bool ready_;
    static unsigned long last_read_;
    static uint8_t state_;

    static bool sendCommand(uint16_t addr, uint32_t value);
    static bool readRegister(uint16_t addr, uint32_t* value);
    static uint8_t calculateChecksum(const uint8_t* data, size_t size);

    static float voltage_;
    static float current_;
    static float power_;
    static float reactive_power_;
    static float power_factor_;
    static float frequency_;
    static float temperature_;
    static float energy_;
};

#endif
