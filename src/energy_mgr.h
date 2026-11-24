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
    // 滚动最近 24 小时逐小时用电量（v5.9）：hh[i] 时间升序（旧→新），
    // 桶数不足 24 时前面补 0；单位 Wh
    static void getHourlySeries(float* out24);
    static int getHourlyCount();
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
    static float last_saved_energy_;
    // 24h 逐小时（v5.9）
    static float hourly_[24];       // 环形桶，hourly_head_ 为当前累积桶
    static int hourly_head_;        // 当前桶索引
    static int hourly_count_;       // 有效桶数 0..24
    static int hourly_last_hour_;   // 上次翻桶的"小时"（墙钟或运行时推算），-1 未初始化
    static float hourly_snapshot_;  // 当前小时起点的 total_energy_
    static float hourly_saved_total_; // 上次持久化时的 total_energy_（重启恢复当前桶）
    static void hourlyRollover(int curHour);
    static void saveHourly();
};

#endif
