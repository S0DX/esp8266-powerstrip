#ifndef ENERGY_MGR_H
#define ENERGY_MGR_H

#include <Arduino.h>
#include <EEPROM.h>

class EnergyManager {
public:
    static void init();
    static void handle();

    static float getTotalEnergy();
    static float getLastValue();
    static void save();
    static void reset();

private:
    static float total_energy_;
    static float last_value_;
    static unsigned long last_save_time_;
};

#endif
