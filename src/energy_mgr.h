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
    static float getHistory(int day);
    static float getMonthlyEnergy();
    static float getLastMonthEnergy();
    static void save();
    static void saveHistory();
    static void saveMonthlyHistory();
    static void reset();

private:
    static float total_energy_;
    static unsigned long last_save_time_;
    static float history_[7];
    static float day_start_energy_;
    static unsigned long day_start_time_;
    static unsigned long last_integration_time_;
    static float month_start_energy_;
    static unsigned long month_start_time_;
    static int month_start_year_;
    static int month_start_month_;
    static float monthly_history_[3];
};

#endif
