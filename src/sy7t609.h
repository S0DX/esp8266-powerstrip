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

    static bool isCalibrated();
    static float getPowerOffset();  // 空载自学习出的功率零点偏移（W），供 UI 展示
    static bool isOffsetLearned();  // 是否已完成一次空载零点自学习

    static void setEnabled(bool enabled);
    static bool isEnabled();
    static bool isFlashMode();
    static void loadFromEEPROM();
    static void saveToEEPROM();

    // -------- 校准功能（P1） --------
    // 重置校准：把原厂默认校准常量写回芯片并保存到 flash
    static bool resetCalibration();
    // 电压自动校准：参数为万用表实测电压（V，如 220.0）
    static bool calibrateVoltage(float realV);
    // 电流自动校准：参数为万用表实测电流（A，如 4.545）
    static bool calibrateCurrent(float realA);
    // 保存当前寄存器到芯片 flash（校准后必须调用）
    static bool saveCalibration();
    // 清零电能计数器（EPPCNT）
    static bool clearEnergyCounter();

    // Debug info for Web UI
    static String getDebugLog();
    static void clearDebugLog();

private:
    static HardwareSerial* serial_;
    static bool ready_;
    static bool enabled_;
    static bool flash_mode_;
    static bool calibrated_;
    static bool gpio0_checked_;
    static unsigned long start_time_;
    static float voltage_;
    static float current_;
    static float power_;
    static float power_factor_;
    static float frequency_;
    static float temperature_;
    static unsigned long last_read_;
    static unsigned long last_init_attempt_;
    static int init_retry_count_;
    static String debug_log_;
    static int debug_log_count_;

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
    static bool readRegister(uint16_t addr, uint32_t* value);  // 同步阻塞，仅供 setup/校准使用
    static bool initializeSY7T609();

    // 自动零漂自学习：检测到长时间稳定空载时，自动学习电流零漂与功率零点
    // 偏移并扣减，免手动校准。空载无大功率电感、真实有功≈0，是唯一的
    // "零点已知"时刻，可安全反推芯片读数偏置。current_ 始终保存原始读数。
    static float current_offset_;      // 电流零漂偏移（A）
    static float power_offset_;        // 功率零点偏移（W），空载芯片有功读数稳态值
    static bool  offset_learned_;      // 是否已完成一次空载零点自学习
    static bool  offset_learning_;     // 正在空载采样
    static unsigned long offset_learn_start_;
    static float offset_learn_min_;    // 空载窗口内电流最小值
    static float offset_learn_pow_sum_;// 空载窗口内功率累加
    static int   offset_learn_pow_cnt_;
    static void applyOffsetLearning(float rawA);  // 由 commitReadResult 对 IRMS 调用

    // P3: 非阻塞状态机（仅供 handle() 使用，彻底消除 100ms 主循环阻塞）
    enum ReadFSMState { FSM_IDLE, FSM_WAITING };
    static ReadFSMState fsm_state_;
    static uint16_t    fsm_addr_;        // 当前读取的寄存器地址
    static uint32_t    fsm_value_;       // 读取结果暂存
    static uint8_t     fsm_retry_;       // 当前重试次数（0..2）
    static unsigned long fsm_start_;     // WAITING 起始时间戳
    static void startReadFSM(uint16_t addr);
    static bool pollReadFSM();           // true=本次 FSM 结束(成功或放弃), false=继续等待
    static void commitReadResult(bool ok);
    static void advanceItem();

    // P2: 表驱动测量项定义
    // 解析类型：SIGNED_MILLI=24bit有符号/1000（PF/POWER/VAR）
    //         UNSIGNED_DIV=无符号/scale（VRMS/IRMS/FREQ/CTEMP）
    //         NONE=不解析（占位/已废弃的 EPPCNT）
    enum ParseType { PARSE_SIGNED_MILLI, PARSE_UNSIGNED_DIV, PARSE_NONE };
    struct MeasurementItem {
        uint16_t addr;
        float* target;
        float scale;
        ParseType parse;
    };
    static MeasurementItem items_[];
    static uint8_t current_item_;
    static const uint8_t ITEM_COUNT_;
};

#endif
