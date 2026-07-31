#include "log_buffer.h"
#include "config.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <ESP.h>
#include <Arduino.h>
#include <EEPROM.h>

static char log_lines[LOG_MAX_LINES][LOG_MAX_LINE_LEN];
static int log_count = 0;
static int log_index = 0;
static bool log_enabled = true;

// ESP.rtcUserMemoryWrite 的 offset 参数单位是 4 字节（uint32_t 字数），不是字节。
// RTC 用户区共 128 个字（512 字节），系统已用前 64 个字，用户可用 offset 0-127。
#define RTC_CRASH_LOG_OFFSET 0     // 对应 RTC 字 64，字节 256
#define RTC_TEST_DATA_OFFSET 100   // 对应 RTC 字 164，字节 656，避免覆盖崩溃日志
#define RTC_CRASH_MAGIC      0xDEADBEEF



struct CrashLogRTC {
    uint32_t magic;
    uint32_t count;
    char logs[CRASH_LOG_MAX_LINES][CRASH_LOG_LINE_LEN];
};

static_assert(sizeof(CrashLogRTC) <= 448, "Crash log RTC struct too large");

// 使用静态缓冲区，确保传递给 SDK 的地址 4 字节对齐
static CrashLogRTC rtc_buf;
static bool rtc_available = false;

void log_buffer_init() {
    log_count = 0;
    log_index = 0;
    for (int i = 0; i < LOG_MAX_LINES; i++) {
        log_lines[i][0] = '\0';
    }

    // 读取系统日志开关，默认启用（未写入或非法值均视为启用）
    uint8_t enabled_val = EEPROM.read(EEPROM_SYS_LOG_ENABLED_ADDR);
    log_enabled = (enabled_val != 0x00);

    // 在启动早期验证 RTC 是否真的能读写回同一个数据
    // 使用独立偏移 RTC_TEST_DATA_OFFSET，避免覆盖 RTC_CRASH_LOG_OFFSET 处的崩溃日志
    uint32_t test_data[2] = {0x12345678, 0xAABBCCDD};
    bool write_ok = ESP.rtcUserMemoryWrite(RTC_TEST_DATA_OFFSET, test_data, sizeof(test_data));
    test_data[0] = 0;
    test_data[1] = 0;
    bool read_ok = ESP.rtcUserMemoryRead(RTC_TEST_DATA_OFFSET, test_data, sizeof(test_data));
    rtc_available = write_ok && read_ok && test_data[0] == 0x12345678 && test_data[1] == 0xAABBCCDD;
}

void log_buffer_printf(const char* fmt, ...) {
    if (!fmt || !log_enabled) return;

    char temp[LOG_MAX_LINE_LEN];
    va_list args;
    va_start(args, fmt);
    vsnprintf(temp, sizeof(temp), fmt, args);
    va_end(args);
    temp[sizeof(temp) - 1] = '\0';

    // 去掉末尾换行，统一存储单行
    size_t len = strlen(temp);
    while (len > 0 && (temp[len - 1] == '\n' || temp[len - 1] == '\r')) {
        temp[--len] = '\0';
    }
    if (len == 0) return;

    strncpy(log_lines[log_index], temp, LOG_MAX_LINE_LEN - 1);
    log_lines[log_index][LOG_MAX_LINE_LEN - 1] = '\0';
    log_index = (log_index + 1) % LOG_MAX_LINES;
    if (log_count < LOG_MAX_LINES) log_count++;

    // RTC 崩溃日志改为由 main loop 末尾显式保存，避免 setup/日志密集时写 RTC
}

bool log_buffer_save_crash_logs() {
    if (!rtc_available) return false;
    memset(&rtc_buf, 0, sizeof(rtc_buf));
    rtc_buf.magic = RTC_CRASH_MAGIC;
    int cnt = log_count;
    if (cnt > CRASH_LOG_MAX_LINES) cnt = CRASH_LOG_MAX_LINES;
    rtc_buf.count = (uint32_t)cnt;
    int start = (log_index - cnt + LOG_MAX_LINES) % LOG_MAX_LINES;
    for (int i = 0; i < cnt; i++) {
        int idx = (start + i) % LOG_MAX_LINES;
        strncpy(rtc_buf.logs[i], log_lines[idx], CRASH_LOG_LINE_LEN - 1);
        rtc_buf.logs[i][CRASH_LOG_LINE_LEN - 1] = '\0';
    }
    return ESP.rtcUserMemoryWrite(RTC_CRASH_LOG_OFFSET, (uint32_t*)&rtc_buf, sizeof(rtc_buf));
}

bool log_buffer_load_crash_logs(char logs_out[][CRASH_LOG_LINE_LEN], int max_logs, int* count) {
    *count = 0;
    if (max_logs <= 0 || !rtc_available) return false;
    memset(&rtc_buf, 0, sizeof(rtc_buf));
    if (!ESP.rtcUserMemoryRead(RTC_CRASH_LOG_OFFSET, (uint32_t*)&rtc_buf, sizeof(rtc_buf))) {
        return false;
    }
    if (rtc_buf.magic != RTC_CRASH_MAGIC) return false;
    int cnt = (int)rtc_buf.count;
    if (cnt > CRASH_LOG_MAX_LINES) cnt = CRASH_LOG_MAX_LINES;
    if (cnt > max_logs) cnt = max_logs;
    for (int i = 0; i < cnt; i++) {
        strncpy(logs_out[i], rtc_buf.logs[i], CRASH_LOG_LINE_LEN - 1);
        logs_out[i][CRASH_LOG_LINE_LEN - 1] = '\0';
    }
    *count = cnt;
    return cnt > 0;
}

bool log_buffer_peek_crash_logs(uint32_t* magic_out, int* count_out) {
    if (!magic_out || !count_out) return false;
    *magic_out = 0;
    *count_out = 0;
    if (!rtc_available) return false;
    memset(&rtc_buf, 0, sizeof(rtc_buf));
    if (!ESP.rtcUserMemoryRead(RTC_CRASH_LOG_OFFSET, (uint32_t*)&rtc_buf, sizeof(rtc_buf))) {
        return false;
    }
    *magic_out = rtc_buf.magic;
    int cnt = (int)rtc_buf.count;
    if (cnt > CRASH_LOG_MAX_LINES) cnt = CRASH_LOG_MAX_LINES;
    *count_out = cnt;
    return true;
}

bool log_buffer_rtc_available() {
    return rtc_available;
}

int log_buffer_count() {
    return log_count;
}

const char* log_buffer_get_line(int index) {
    if (index < 0 || index >= log_count) return "";
    int start = (log_index - log_count + LOG_MAX_LINES) % LOG_MAX_LINES;
    int idx = (start + index) % LOG_MAX_LINES;
    return log_lines[idx];
}

bool log_buffer_is_enabled() {
    return log_enabled;
}

void log_buffer_set_enabled(bool enabled) {
    log_enabled = enabled;
    EEPROM.write(EEPROM_SYS_LOG_ENABLED_ADDR, enabled ? 0x01 : 0x00);
    EEPROM.commit();
}
