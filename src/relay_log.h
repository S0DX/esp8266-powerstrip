#ifndef RELAY_LOG_H
#define RELAY_LOG_H

#include <Arduino.h>

/**
 * 继电器动作日志（持久化，掉电保留）
 *
 * 用途：事后分析"到点有没有上电"。存 EEPROM 环形缓冲，固定条数，
 * 写满后用最新记录覆盖最旧记录（不增长、不占用 RAM 长驻空间）。
 *
 * 每条 6 字节：unix 时间戳(4, LE, 0=当时时间无效) + 事件码(1) + 状态(1)
 * 存储布局：meta(2B: head, count) + data(N * 6B)
 */
class RelayLog {
public:
    // 事件码
    enum Code : uint8_t {
        EV_CHANGE = 1,      // 继电器状态变化（主/副见 status 位）
        EV_CYCLE_ON = 2,    // 24h 循环进入开时段
        EV_CYCLE_OFF = 3,   // 24h 循环离开开时段
        EV_TIMER_START = 4, // 倒计时启动
        EV_TIMER_STOP = 5,  // 倒计时停止/到期
        EV_LATCH_ON = 6,    // 长通开启
        EV_LATCH_OFF = 7,   // 长通关闭
        EV_BILLING = 8,     // 计费停/开关联动作
        EV_BOOT = 9         // 上电启动（时间未就绪时也记，便于确认断电）
    };

    static const uint8_t MAX_ENTRIES = 24;

    static void init();
    // 追加一条记录（环形覆盖最旧）；返回 false 表示未启用存储
    static bool add(uint8_t code, bool master, bool slave);
    // 格式化为多行文本（新→旧），返回写入长度
    static int dump(char* out, size_t outSize);
    static uint8_t count();

private:
    static uint8_t head_;    // 下一条写入位置
    static uint8_t count_;   // 已存条数（<=MAX_ENTRIES）
};

#endif
