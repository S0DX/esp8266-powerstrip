#ifndef TIME_MGR_H
#define TIME_MGR_H

#include <Arduino.h>

/**
 * 时间同步管理器（唯一时间源出口）
 *
 * 优先级：NTP 联网自动校时 > 浏览器手动校正 > 无效（未校时）
 *
 * 行为约定：
 *  1. 联网且 SNTP 已完成同步时，拒绝浏览器校时（避免手机时间污染准确时钟）；
 *  2. 离线或 NTP 不可达时，接受浏览器校时作为回退；
 *  3. 离线期间系统 RTC 持续自走时，定时器/24h 循环照常工作，无需全程联网；
 *  4. 本类不读写 EEPROM —— 断电重启后时间为空，必须重新校时，这就是
 *     "断电后必须重新获取时间"的实现方式（不持久化 = 不会带着旧时间上电）。
 */
class TimeManager {
public:
    enum Source { SRC_NONE = 0, SRC_NTP = 1, SRC_BROWSER = 2 };

    static void init();
    static void handle();

    // 时间是否可用（已完成至少一次校时）
    static bool isValid();
    // 统一时间出口：已校时后即使断网也持续走时
    static time_t now();
    static Source source();
    // 距最近一次校时的秒数（未校时返回 0）
    static uint32_t syncAgeSec();
    static uint32_t lastSyncUnix();
    // NTP 当前是否权威（联网 + 本会话 SNTP 成功 + 时间有效）
    static bool ntpAvailable();

    /**
     * 浏览器校时入口。
     * @param ts     浏览器 UTC 秒（Date.now()/1000）
     * @param reason 输出拒绝/采用原因："ntp" 已联网校时被拒，"invalid" 参数非法，
     *               "browser" 已采用
     * @return true 表示已采用
     */
    static bool applyBrowserTime(time_t ts, const char** reason);

    static void onNetworkUp();
    static void onNetworkDown();
    static void requestNtpSync();

private:
    static Source source_;
    static bool ntp_session_ok_;          // 本次联网会话内 SNTP 是否已成功
    static uint32_t last_sync_unix_;
    static unsigned long last_sync_millis_;
    static unsigned long last_ntp_request_millis_;
    static bool ntp_requested_;
};

#endif
