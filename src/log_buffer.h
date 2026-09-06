#ifndef LOG_BUFFER_H
#define LOG_BUFFER_H

#include <stdint.h>

#define LOG_MAX_LINES     24
#define LOG_MAX_LINE_LEN  128

void log_buffer_init();
void log_buffer_printf(const char* fmt, ...);
int log_buffer_count();
const char* log_buffer_get_line(int index);

// 系统日志总开关（默认启用）
bool log_buffer_is_enabled();
void log_buffer_set_enabled(bool enabled);

// 崩溃前日志持久化到 RTC 内存（软复位/看门狗复位保留，掉电丢失）
// 缩小到 1 条/80 字节，排除 RTC 写入大小限制问题
#define CRASH_LOG_MAX_LINES 1
#define CRASH_LOG_LINE_LEN  80
bool log_buffer_save_crash_logs();
bool log_buffer_load_crash_logs(char logs_out[][CRASH_LOG_LINE_LEN], int max_logs, int* count);
// 调试用：读取 RTC 原始 magic/count，不依赖输出缓冲区
bool log_buffer_peek_crash_logs(uint32_t* magic_out, int* count_out);
// 返回 RTC 是否在启动时通过读写校验
bool log_buffer_rtc_available();

#endif
