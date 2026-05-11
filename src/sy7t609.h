#ifndef SY7T609_H
#define SY7T609_H

#include <Arduino.h>
#include <EEPROM.h>
#include <SoftwareSerial.h>

class SY7T609 {
public:
    static void init();
    static void handle();

    static bool isReady();
    static float getVoltage();
    static float getCurrent();
    static float getPower();
    static float getPowerFactor();
    static float getFrequency();
    static float getTemperature();
    static float getEnergy();

    static void resetEnergy();
    static void setEnabled(bool enabled);
    static bool isEnabled();
    static bool isFlashMode();
    static void loadFromEEPROM();
    static void saveToEEPROM();
    
    // Debug info for Web UI
    static String getDebugLog();
    static void clearDebugLog();

private:
    static HardwareSerial* serial_;
    static bool ready_;
    static bool enabled_;
    static bool flash_mode_;
    static bool gpio0_checked_;
    static unsigned long start_time_;
    static float voltage_;
    static float current_;
    static float power_;
    static float power_factor_;
    static float frequency_;
    static float temperature_;
    static float energy_;
    static unsigned long last_read_;
    static bool auto_report_;
    static unsigned long last_init_attempt_;
    static int init_retry_count_;
    static String debug_log_;
    static int debug_log_count_;
    static uint8_t read_state_;

    static float voltage_scale_;
    static float current_scale_;
    static float power_scale_;
    static float pf_scale_;
    static float frequency_scale_;
    static float temperature_scale_;

    static bool checkGPIO0ForFlashMode();
    static bool canUseSerial();
    static uint8_t calculateChecksum(const uint8_t* data, size_t size);
    static bool sendCommand(uint16_t addr, uint32_t value);
    static bool readRegister(uint16_t addr, uint32_t* value);
    static bool initializeSY7T609();
};

#endif
