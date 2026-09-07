#include "sy7t609.h"
#include "sy7t609_def.h"
#include "config.h"
#include <Arduino.h>
#include <ESP.h>

HardwareSerial* SY7T609::serial_ = nullptr;
bool SY7T609::ready_ = false;
bool SY7T609::enabled_ = true;
bool SY7T609::flash_mode_ = false;
bool SY7T609::calibrated_ = false;
bool SY7T609::gpio0_checked_ = false;
unsigned long SY7T609::start_time_ = 0;
float SY7T609::voltage_ = 0.0f;
float SY7T609::current_ = 0.0f;
float SY7T609::power_ = 0.0f;
float SY7T609::power_factor_ = 0.0f;
float SY7T609::frequency_ = 0.0f;
float SY7T609::temperature_ = 0.0f;
unsigned long SY7T609::last_read_ = 0;
unsigned long SY7T609::last_init_attempt_ = 0;
int SY7T609::init_retry_count_ = 0;
String SY7T609::debug_log_ = "";
int SY7T609::debug_log_count_ = 0;

// P3: 非阻塞读取状态机上下文
SY7T609::ReadFSMState SY7T609::fsm_state_ = FSM_IDLE;
uint16_t    SY7T609::fsm_addr_   = 0;
uint32_t    SY7T609::fsm_value_  = 0;
uint8_t     SY7T609::fsm_retry_  = 0;
unsigned long SY7T609::fsm_start_ = 0;

// P2: 表驱动测量项（顺序与原 switch-case 一致）
// 添加新测量项只需在此表追加一行，无需修改 handle() 逻辑
SY7T609::MeasurementItem SY7T609::items_[] = {
    { ADDR_PF,        &power_factor_, 0,    PARSE_SIGNED_MILLI },  // PF（保留正负号）
    { ADDR_VRMS,      &voltage_,      1000.0f, PARSE_UNSIGNED_DIV },  // 电压
    { ADDR_IRMS,      &current_,      1000.0f, PARSE_UNSIGNED_DIV },  // 电流
    { ADDR_POWER,     &power_,        0,    PARSE_SIGNED_MILLI },  // 有功功率（保留正负号）
    { ADDR_VAR,       nullptr,        0,    PARSE_NONE },          // 无功功率（占位，未对外暴露）
    { 0,              nullptr,        0,    PARSE_NONE },          // 占位（原 EPPCNT 已废弃）
    { ADDR_FREQUENCY, &frequency_,    1000.0f, PARSE_UNSIGNED_DIV },  // 频率
    { ADDR_CTEMP,     &temperature_,  1000.0f, PARSE_UNSIGNED_DIV },  // 芯片温度
};
uint8_t SY7T609::current_item_ = 0;
const uint8_t SY7T609::ITEM_COUNT_ = sizeof(items_) / sizeof(items_[0]);

float SY7T609::voltage_scale_ = 1000.0f;
float SY7T609::current_scale_ = 1000.0f;
float SY7T609::power_scale_ = 1000.0f;
float SY7T609::pf_scale_ = 1000.0f;
float SY7T609::frequency_scale_ = 1000.0f;
float SY7T609::temperature_scale_ = 1000.0f;

// 自动零漂自学习状态
float SY7T609::current_offset_ = 0.0f;
float SY7T609::power_offset_ = 0.0f;
bool  SY7T609::offset_learned_ = false;
bool  SY7T609::offset_learning_ = false;
unsigned long SY7T609::offset_learn_start_ = 0;
float SY7T609::offset_learn_min_ = 1000.0f;
float SY7T609::offset_learn_pow_sum_ = 0.0f;
int   SY7T609::offset_learn_pow_cnt_ = 0;

// 零漂自学习参数
#define OFFSET_LEARN_BAND_A  0.035f   // 电流低于此值视为"空载"（含零漂+容性漏流）
#define OFFSET_MIN_STABLE_MS 4000     // 空载需稳定持续的时间（ms）
#define OFFSET_MAX_A         0.09f    // 电流偏移上限，防止把真实小负载误学成偏移
#define OFFSET_LERP_RATE     0.10f    // 偏移缓慢逼近速率（防跳变）
#define OFFSET_POW_MAX_W     3.0f     // 功率偏移上限（空载芯片有功读数远小于此）

bool g_meter_enabled = false;

#define SY7T609_READ_INTERVAL 1000
#define SY7T609_STARTUP_DELAY_MS 2000
#define SY7T609_INIT_RETRY_MAX 3
#define SY7T609_INIT_RETRY_DELAY_MS 500
#define GPIO0_CHECK_TIMEOUT_MS 100

// 寄存器地址、SSI 协议常量、校准常量统一来自 sy7t609_def.h
// 以下为简短别名，保持本文件原有代码可读性
constexpr uint8_t CMD_SELECT_ADDR   = CMD_SELECT_REGISTER_ADDRESS;
constexpr uint8_t CMD_READ_REG      = CMD_READ_REGITSTER_3BYTES;
constexpr uint8_t CMD_WRITE_REG     = CMD_WRITE_RETISTER_3BYTES;
constexpr uint8_t REPLY_ACK         = REPLY_ACK_WITH_DATA;
constexpr uint8_t REPLY_ACK_NO_DATA = REPLY_ACK_WITHOUT_DATA;

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
        // 拆分 delay 并在期间喂狗，避免 setup 阶段触发 Hardware Watchdog
        for (int i = 0; i < 2; i++) {
            delay(50);
            ESP.wdtFeed();
            yield();
        }
    }

    while (serial_->available()) {
        serial_->read();
    }

    sendCommand(ADDR_COMMAND, CMD_REG_SOFT_RESET);
    // 拆分 delay 并在期间喂狗，避免 setup 阶段触发 Hardware Watchdog
    for (int i = 0; i < 2; i++) {
        delay(50);
        ESP.wdtFeed();
        yield();
    }

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
        // 拆分重试延迟并喂狗
        for (int i = 0; i < SY7T609_INIT_RETRY_DELAY_MS / 50; i++) {
            delay(50);
            ESP.wdtFeed();
            yield();
        }
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

    // P3: 非阻塞状态机驱动，每次进 handle() 只推进一个状态，彻底消除 100ms 阻塞
    switch (fsm_state_) {
        case FSM_IDLE: {
            if (millis() - last_read_ < SY7T609_READ_INTERVAL) return;
            if (current_item_ >= ITEM_COUNT_) current_item_ = 0;

            const MeasurementItem& item = items_[current_item_];
            // PARSE_NONE 项直接跳过（占位），不进入 FSM
            if (item.parse == PARSE_NONE || item.target == nullptr) {
                advanceItem();
                last_read_ = millis();
                return;
            }
            startReadFSM(item.addr);
            fsm_state_ = FSM_WAITING;
            break;
        }
        case FSM_WAITING: {
            // pollReadFSM 返回 true 表示本次 FSM 结束（成功或放弃）
            if (pollReadFSM()) {
                fsm_state_ = FSM_IDLE;
                last_read_ = millis();  // FSM 结束后重置间隔计时
            }
            break;
        }
    }
}

// P3: 启动一次非阻塞读取（构造并发送 7 字节请求帧）
void SY7T609::startReadFSM(uint16_t addr) {
    fsm_addr_ = addr;
    fsm_retry_ = 0;

    uint8_t tx[7];
    tx[0] = SSI_HEADER;
    tx[1] = 0x07;
    tx[2] = CMD_SELECT_ADDR;
    tx[3] = addr & 0xFF;
    tx[4] = (addr >> 8) & 0xFF;
    tx[5] = CMD_READ_REG;
    tx[6] = calculateChecksum(tx, 7);

    while (serial_->available()) serial_->read();  // 清空接收缓冲，避免脏数据
    serial_->flush();
    serial_->write(tx, 7);
    fsm_start_ = millis();
}

// P3: 非阻塞轮询接收响应
// 返回 true: 本次 FSM 结束（成功或重试耗尽放弃）
// 返回 false: 继续等待（下次 handle() 再轮询）
bool SY7T609::pollReadFSM() {
    // 检查是否有完整响应帧（6 字节）
    if (serial_->available() >= 6) {
        uint8_t rx[6];
        for (int i = 0; i < 6; i++) rx[i] = serial_->read();

        if (rx[0] == REPLY_ACK) {
            uint8_t cs = calculateChecksum(rx, 6);
            if (cs == rx[5]) {
                fsm_value_ = ((uint32_t)rx[4] << 16) | ((uint32_t)rx[3] << 8) | rx[2];
                commitReadResult(true);
                return true;
            }
        }
        // 帧错误：不立即放弃，继续等待直到超时
    }

    // 超时检查（与原 readRegister 一致的 50ms 阈值）
    if (millis() - fsm_start_ < 50) return false;

    // 超时，重试（最多 2 次，与原实现一致）
    if (++fsm_retry_ < 2) {
        startReadFSM(fsm_addr_);
        return false;
    }

    // 重试耗尽，放弃当前项（行为改进：原版卡住重试，新版推进到下一项，避免单项故障阻塞整体轮询）
    commitReadResult(false);
    return true;
}

// P3: 自动零漂自学习（对每次 IRMS 原始读数调用）
// 空载无大功率电感、真实有功功率≈0，是唯一的"零点已知"时刻。检测到长时间
// 稳定空载时，自动学习两个偏移并在读取时扣减：
//   1. current_offset_：电流零漂/容性漏流（A）
//   2. power_offset_  ：芯片有功功率零点的读数偏置（W），解决空载负功率
void SY7T609::applyOffsetLearning(float rawA) {
    float loadI = rawA - current_offset_;  // 更纯的空载判据（规避已学的电流偏移）
    if (loadI < OFFSET_LEARN_BAND_A) {
        // 处于空载区间：累积窗口，追踪电流最小值、汇总功率
        if (!offset_learning_) {
            offset_learning_ = true;
            offset_learn_start_ = millis();
            offset_learn_min_ = rawA;
            offset_learn_pow_sum_ = power_;
            offset_learn_pow_cnt_ = 1;
        } else {
            if (rawA < offset_learn_min_) offset_learn_min_ = rawA;
            offset_learn_pow_sum_ += power_;
            offset_learn_pow_cnt_++;
            // 稳定空载持续足够久，才计入偏移。空载窗口经电流阈值+时长双重过滤，
            // 采样可信，直接赋值以免 lerp 收敛过慢导致 UI 长期停留在"自学习中"。
            if (millis() - offset_learn_start_ >= OFFSET_MIN_STABLE_MS) {
                // 电流零漂用窗口最小值（最接近真零漂）
                if (offset_learn_min_ < OFFSET_MAX_A) {
                    current_offset_ = offset_learn_min_;
                    if (current_offset_ < 0.0f) current_offset_ = 0.0f;
                }
                // 功率零点偏移用空载芯片有功读数的均值（真实有功≈0，均值即偏置）
                float meanPow = offset_learn_pow_sum_ / offset_learn_pow_cnt_;
                if (meanPow > -OFFSET_POW_MAX_W && meanPow < OFFSET_POW_MAX_W) {
                    power_offset_ = meanPow;
                }
                // 滚动窗口继续追踪可能更低的空载值
                offset_learn_start_ = millis();
                offset_learn_min_ = rawA;
                offset_learn_pow_sum_ = power_;
                offset_learn_pow_cnt_ = 1;
                offset_learned_ = true;
            }
        }
    } else {
        // 出现真实负载，停止学习
        offset_learning_ = false;
    }
}

// P3: 处理读取结果（解析 + debug log + 推进 item）
void SY7T609::commitReadResult(bool ok) {
    const MeasurementItem& item = items_[current_item_];

    if (ok) {
        switch (item.parse) {
            case PARSE_SIGNED_MILLI:
                *item.target = parse_signed_milli(fsm_value_);
                break;
            case PARSE_UNSIGNED_DIV:
                *item.target = (item.scale != 0) ? (fsm_value_ / item.scale) : 0;
                break;
            case PARSE_NONE:
                break;
        }
        // 对 IRMS 原始读数做零漂自学习（必须在扣减偏移之前）
        if (item.addr == ADDR_IRMS) {
            applyOffsetLearning(current_);
        }
        // 简化的 debug 日志（仅前 20 次）
        if (debug_log_count_ < 20) {
            char log_buf[80];
            snprintf(log_buf, sizeof(log_buf), "[SY7T609] addr=0x%04X raw=%u val=%.3f",
                     item.addr, fsm_value_, *item.target);
            debug_log_ += String(log_buf) + "\n";
            debug_log_count_++;
        }
    }
    // 失败时不更新 *item.target（保留上次值），但推进到下一项

    advanceItem();
}

void SY7T609::advanceItem() {
    current_item_++;
    if (current_item_ >= ITEM_COUNT_) {
        current_item_ = 0;
        ready_ = true;
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
    uint8_t val = EEPROM.read(EEPROM_SY7T609_ENABLED_ADDR);
    if (val == 0xFF) {
        // EEPROM 未初始化，默认开启电量检测
        enabled_ = true;
        EEPROM.write(EEPROM_SY7T609_ENABLED_ADDR, 1);
        EEPROM.commit();
        Serial.println("[SY7T609] EEPROM uninitialized, default enabled");
    } else {
        enabled_ = (val == 1);
    }
    calibrated_ = (EEPROM.read(EEPROM_SY7T609_CALIB_FLAG_ADDR) == EEPROM_SY7T609_CALIB_MAGIC);
    Serial.printf("[SY7T609] Loaded from EEPROM: %s, calibrated: %s\n",
                  enabled_ ? "enabled" : "disabled", calibrated_ ? "yes" : "no");
}

void SY7T609::saveToEEPROM() {
    EEPROM.write(EEPROM_SY7T609_ENABLED_ADDR, enabled_ ? 1 : 0);
    EEPROM.commit();
    Serial.printf("[SY7T609] Saved to EEPROM: %s\n", enabled_ ? "enabled" : "disabled");
}

// ============================================================================
// 校准功能实现（P1）
// 原厂校准数据来自 sy7t609_def.h 中的 DEFAULT_* 常量
// 自动校准流程：写 TARGET 寄存器 → 发校准命令 → 等待 → 保存到 flash
// ============================================================================

bool SY7T609::saveCalibration() {
    if (!ready_ || !canUseSerial()) {
        Serial.println(F("[SY7T609] saveCalibration: not ready"));
        return false;
    }
    // 发送 SAVE_TO_FLASH 命令，将当前寄存器值保存到芯片内部 flash
    if (!sendCommand(ADDR_COMMAND, CMD_REG_SAVE_TO_FLASH)) {
        Serial.println(F("[SY7T609] saveCalibration: sendCommand failed"));
        return false;
    }
    delay(120);  // 保存到 flash 需要 ~100ms
    ESP.wdtFeed();
    Serial.println(F("[SY7T609] Calibration saved to flash"));
    return true;
}

bool SY7T609::resetCalibration() {
    if (!ready_ || !canUseSerial()) {
        Serial.println(F("[SY7T609] resetCalibration: not ready"));
        return false;
    }
    Serial.println(F("[SY7T609] Resetting calibration to factory defaults..."));

    // 写入原厂默认校准常量到芯片寄存器
    struct { uint16_t addr; uint32_t value; const char* name; } calib_table[] = {
        { ADDR_ISCALE,      DEFAULT_ISCALE,      "ISCALE"      },
        { ADDR_VSCALE,      DEFAULT_VSCALE,      "VSCALE"      },
        { ADDR_PSCALE,      DEFAULT_PSCALE,      "PSCALE"      },
        { ADDR_ACCUM,       DEFAULT_ACCUM,       "ACCUM"       },
        { ADDR_IRMS_TARGET, DEFAULT_IRMS_TARGET, "IRMS_TARGET" },
        { ADDR_VRMS_TARGET, DEFAULT_VRMS_TARGET, "VRMS_TARGET" },
        { ADDR_POWER_TARGET,DEFAULT_POWER_TARGET,"POWER_TARGET"},
        { ADDR_CONTROL,     DEFAULT_CONTROL,     "CONTROL"     },
        { ADDR_BUCKETH,     DEFAULT_BUCKETH,     "BUCKETH"     },
        { ADDR_BUCKETL,     DEFAULT_BUCKETL,     "BUCKETL"     },
        { ADDR_IGAIN,       DEFAULT_IGAIN,       "IGAIN"       },
        { ADDR_VGAIN,       DEFAULT_VGAIN,       "VGAIN"       },
    };
    const int table_size = sizeof(calib_table) / sizeof(calib_table[0]);

    for (int i = 0; i < table_size; i++) {
        if (!sendCommand(calib_table[i].addr, calib_table[i].value)) {
            Serial.printf("[SY7T609] Failed to write %s\n", calib_table[i].name);
            return false;
        }
        delay(10);
        ESP.wdtFeed();  // 防止看门狗复位
    }
    delay(50);

    // 保存到芯片 flash
    bool ok = saveCalibration();
    if (ok) {
        // 恢复原厂常量后相位/增益误差依旧存在，功率退回 V×I 估算
        calibrated_ = false;
        EEPROM.write(EEPROM_SY7T609_CALIB_FLAG_ADDR, 0);
        EEPROM.commit();
        Serial.println(F("[SY7T609] Calibration reset complete, power source: V*I"));
    }
    return ok;
}

bool SY7T609::calibrateVoltage(float realV) {
    if (!ready_ || !canUseSerial()) {
        Serial.println(F("[SY7T609] calibrateVoltage: not ready"));
        return false;
    }
    if (realV <= 0 || realV > 300) {
        Serial.printf("[SY7T609] calibrateVoltage: invalid voltage %.2f\n", realV);
        return false;
    }
    // VRMS_TARGET 单位为 mV，24bit 无符号
    uint32_t target = static_cast<uint32_t>(realV * 1000.0f) & 0xFFFFFF;
    Serial.printf("[SY7T609] Calibrating voltage, target=%.2fV (0x%06X)\n", realV, target);

    // 1. 写入校准目标值
    if (!sendCommand(ADDR_VRMS_TARGET, target)) return false;
    delay(20);
    ESP.wdtFeed();

    // 2. 发送电压自动校准命令
    if (!sendCommand(ADDR_COMMAND, CMD_REG_CALIBRATION_VOLTAGE)) return false;
    delay(500);  // 自动校准需要较长时间收敛
    ESP.wdtFeed();

    // 3. 保存到芯片 flash
    if (!saveCalibration()) return false;
    calibrated_ = true;
    EEPROM.write(EEPROM_SY7T609_CALIB_FLAG_ADDR, EEPROM_SY7T609_CALIB_MAGIC);
    EEPROM.commit();
    Serial.println(F("[SY7T609] Calibration flag set, power source: chip active power"));
    return true;
}

bool SY7T609::calibrateCurrent(float realA) {
    if (!ready_ || !canUseSerial()) {
        Serial.println(F("[SY7T609] calibrateCurrent: not ready"));
        return false;
    }
    if (realA <= 0 || realA > 100) {
        Serial.printf("[SY7T609] calibrateCurrent: invalid current %.3f\n", realA);
        return false;
    }
    // IRMS_TARGET 单位为 mA，24bit 无符号
    uint32_t target = static_cast<uint32_t>(realA * 1000.0f) & 0xFFFFFF;
    Serial.printf("[SY7T609] Calibrating current, target=%.3fA (0x%06X)\n", realA, target);

    // 1. 写入校准目标值
    if (!sendCommand(ADDR_IRMS_TARGET, target)) return false;
    delay(20);
    ESP.wdtFeed();

    // 2. 发送电流自动校准命令
    if (!sendCommand(ADDR_COMMAND, CMD_REG_CALIBRATION_CURRENT)) return false;
    delay(500);
    ESP.wdtFeed();

    // 3. 保存到芯片 flash
    if (!saveCalibration()) return false;
    calibrated_ = true;
    EEPROM.write(EEPROM_SY7T609_CALIB_FLAG_ADDR, EEPROM_SY7T609_CALIB_MAGIC);
    EEPROM.commit();
    Serial.println(F("[SY7T609] Calibration flag set, power source: chip active power"));
    return true;
}

bool SY7T609::clearEnergyCounter() {
    if (!ready_ || !canUseSerial()) {
        Serial.println(F("[SY7T609] clearEnergyCounter: not ready"));
        return false;
    }
    if (!sendCommand(ADDR_COMMAND, CMD_REG_CLEAR_ENGERGY_COUNTERS)) return false;
    delay(50);
    ESP.wdtFeed();
    Serial.println(F("[SY7T609] Energy counter cleared"));
    return true;
}

// 交流空载死区：零漂自学习基础上再兜底，电流仍极低时强制归零（防极端毛刺）
#define SY7T609_IRMS_DEADZONE_A 0.002f

float SY7T609::getVoltage() { return voltage_; }
float SY7T609::getCurrent() {
    // 扣减自动学习的电流零漂偏移；current_ 始终为原始读数
    float v = current_ - current_offset_;
    return v < 0.0f ? 0.0f : v;
}
float SY7T609::getPower() {
    // 始终采用芯片有功功率（含功率因数语义、稳定）。空载时芯片读数存在
    // 加性零点偏置（可能为负），由空载自学习出的 power_offset_ 扣除；
    // 电流死区仅兜底极端毛刺，避免空载小抖动。
    float p = power_ - power_offset_;
    if (getCurrent() < SY7T609_IRMS_DEADZONE_A) p = 0.0f;
    return p;
}
float SY7T609::getPowerOffset() { return power_offset_; }
bool SY7T609::isOffsetLearned() { return offset_learned_; }
float SY7T609::getPowerFactor() { return power_factor_; }
float SY7T609::getFrequency() { return frequency_; }
float SY7T609::getTemperature() { return temperature_; }
bool SY7T609::isCalibrated() { return calibrated_; }

String SY7T609::getDebugLog() {
    return debug_log_;
}

void SY7T609::clearDebugLog() {
    debug_log_ = "";
    debug_log_count_ = 0;
}
