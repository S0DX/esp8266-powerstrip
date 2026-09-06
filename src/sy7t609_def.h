#pragma once
#include <cstdint>

// ============================================================================
// SY7T609 寄存器地址表、SSI 协议常量、校准参数默认值
// 来源：cmpower-firmware 反编译固件（原厂校准数据）
// ============================================================================

// ----------------------------------------------------------------------------
// 校准 / 缩放寄存器固定值
// reset_calibration 操作会把这些值写回芯片并存入 flash。
// 这些数据来自原厂万用表校准，适用于 SY7T609 + 分压/分流电路的常见设计。
// ----------------------------------------------------------------------------
constexpr uint32_t DEFAULT_ISCALE       = 0x00F230;
constexpr uint32_t DEFAULT_VSCALE       = 0x0A2D78;
constexpr uint32_t DEFAULT_PSCALE       = 0x027703;
constexpr uint32_t DEFAULT_ACCUM        = 0x001A2C;
constexpr uint32_t DEFAULT_IRMS_TARGET  = 0x0003E8;
constexpr uint32_t DEFAULT_VRMS_TARGET  = 0x03807C;
constexpr uint32_t DEFAULT_POWER_TARGET = 0x01D4C0;
constexpr uint32_t DEFAULT_CONTROL      = 0x001817;
constexpr uint32_t DEFAULT_BUCKETH      = 0x000247;
constexpr uint32_t DEFAULT_BUCKETL      = 0x7780A5;  // 原厂默认数据
constexpr uint32_t DEFAULT_IGAIN        = 0x43DF0D;  // 电流增益（万用表校准值）
constexpr uint32_t DEFAULT_VGAIN        = 0x210A40;  // 电压增益

// ----------------------------------------------------------------------------
// SSI 协议常量
// ----------------------------------------------------------------------------
constexpr uint8_t SSI_HEADER               = 0xAA;
constexpr uint8_t SSI_DEFAULT_FRAME_SIZE   = 3;
constexpr uint8_t SSI_MAX_PAYLOAD_SIZE     = 7;
constexpr uint8_t SSI_READ_PAYLOAD_SIZE    = 4;
constexpr uint8_t SSI_REPLY_PAYLOAD_SIZE   = 3;
constexpr uint8_t SSI_WRITE_PAYLOAD_SIZE   = 7;
constexpr uint8_t SSI_UART_READ_SEND_PKG_SIZE  = SSI_DEFAULT_FRAME_SIZE + SSI_READ_PAYLOAD_SIZE;   // 7
constexpr uint8_t SSI_UART_WRITE_SEND_PKG_SIZE = SSI_DEFAULT_FRAME_SIZE + SSI_WRITE_PAYLOAD_SIZE;  // 10
constexpr uint8_t SSI_UART_READ_RECV_PKG_SIZE  = SSI_DEFAULT_FRAME_SIZE + SSI_REPLY_PAYLOAD_SIZE;  // 6

// ----------------------------------------------------------------------------
// SSI 命令字
// ----------------------------------------------------------------------------
constexpr uint8_t CMD_SELECT_REGISTER_ADDRESS = 0xA3;
constexpr uint8_t CMD_READ_REGITSTER_3BYTES   = 0xE3;
constexpr uint8_t CMD_WRITE_RETISTER_3BYTES   = 0xD3;

// ----------------------------------------------------------------------------
// 寄存器地址表
// ----------------------------------------------------------------------------
enum sy7t609_register_map {
    ADDR_COMMAND       = 0x0000,
    ADDR_CONTROL       = 0x0006,
    // Metering Address（测量值）
    ADDR_CTEMP         = 0x0027,  // 芯片温度
    ADDR_VAVG          = 0x002D,  // 电压平均值
    ADDR_IAVG          = 0x0030,  // 电流平均值
    ADDR_VRMS          = 0x0033,  // 电压有效值
    ADDR_IRMS          = 0x0036,  // 电流有效值
    ADDR_POWER         = 0x0039,  // 有功功率
    ADDR_VAR           = 0x003C,  // 无功功率
    ADDR_FREQUENCY     = 0x0042,  // 频率（保留兼容）
    ADDR_PF            = 0x0048,  // 功率因数
    ADDR_EPPCNT        = 0x0069,  // 正向有功电能计数
    // Calibration Address（校准寄存器）
    ADDR_BUCKETL       = 0x00C0,
    ADDR_BUCKETH       = 0x00C3,
    ADDR_IGAIN         = 0x00D5,  // 电流增益
    ADDR_VGAIN         = 0x00D8,  // 电压增益
    ADDR_ISCALE        = 0x00ED,  // 电流缩放
    ADDR_VSCALE        = 0x00F0,  // 电压缩放
    ADDR_PSCALE        = 0x00F3,  // 功率缩放
    ADDR_ACCUM         = 0x0105,  // 累加系数
    ADDR_IAVG_TARGET   = 0x0111,
    ADDR_VAVG_TARGET   = 0x0114,
    ADDR_IRMS_TARGET   = 0x0117,  // 电流校准目标
    ADDR_VRMS_TARGET   = 0x011A,  // 电压校准目标
    ADDR_POWER_TARGET  = 0x011D,  // 功率校准目标
    ADDR_ERROR         = 0x0FFF
};

// ----------------------------------------------------------------------------
// COMMAND 寄存器命令码
// ----------------------------------------------------------------------------
enum command_register_code {
    CMD_NONE                       = 0x000000,
    CMD_REG_CLEAR_ENGERGY_COUNTERS = 0xEC0000,  // 清除所有电能计数器
    CMD_REG_SOFT_RESET             = 0xBD0000,  // 软复位
    CMD_REG_SAVE_TO_FLASH          = 0xACC200,  // 保存当前寄存器到 flash
    // 自动校准命令（0xCA 前缀，低 16 位为校准目标位掩码）
    CMD_REG_CALIBRATION_VOLTAGE    = 0xCA0020,  // 电压增益自动校准
    CMD_REG_CALIBRATION_CURRENT    = 0xCA0010   // 电流增益自动校准
};

// ----------------------------------------------------------------------------
// SSI 回复码
// ----------------------------------------------------------------------------
enum sy7t609_reply_code {
    REPLY_ACK_WITH_DATA           = 0xAA,
    REPLY_AUTO_REPORTING_HEADER   = 0xAE,
    REPLY_ACK_WITHOUT_DATA        = 0xAD,
    REPLY_NEGATIVE_ACK            = 0xB0,
    REPLY_COMMAND_NOT_IMPLEMENTED = 0xBC,
    REPLY_CHECKSUM_FAILED         = 0xBD,
    REPLY_BUFFER_OVERFLOW         = 0xBF
};

// ----------------------------------------------------------------------------
// 24bit 数据解析工具函数
// ----------------------------------------------------------------------------

// 无符号解析：raw / 1000（电压、电流、温度）
inline float parse_milli(uint32_t raw) {
    return static_cast<float>(raw) / 1000.0f;
}

// 24bit 有符号解析（保留正负号）：用于功率、无功功率、功率因数
// 正值=正向（用电/吸收），负值=反向（光伏倒送、容性无功）
inline float parse_signed_milli(uint32_t raw) {
    int32_t v = (raw >= 0x800000) ?
                (static_cast<int32_t>(raw) - 0x01000000) :
                static_cast<int32_t>(raw);
    return static_cast<float>(v) / 1000.0f;
}

// 24bit 有符号解析（取绝对值）：用于需要幅值显示的场景
inline float parse_abs_milli(uint32_t raw) {
    uint32_t v = (raw >= 0x800000) ? (0x01000000u - raw) : raw;
    return static_cast<float>(v) / 1000.0f;
}

// 原始值直接返回（电量计数器）
inline float parse_raw(uint32_t raw) {
    return static_cast<float>(raw);
}
