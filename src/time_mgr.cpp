#include "time_mgr.h"
#include "config.h"

#include <time.h>
#include <sys/time.h>
#include <stdlib.h>
#include <ESP8266WiFi.h>
#include <lwip/apps/sntp.h>

// 时间源状态（全部为 RAM 变量：掉电即失，重启后必须重新校时）
TimeManager::Source TimeManager::source_ = TimeManager::SRC_NONE;
bool          TimeManager::ntp_session_ok_ = false;
uint32_t      TimeManager::last_sync_unix_ = 0;
unsigned long TimeManager::last_sync_millis_ = 0;
unsigned long TimeManager::last_ntp_request_millis_ = 0;
bool          TimeManager::ntp_requested_ = false;

// SNTP 服务器可达性：收到过任一台服务器的响应即视为联网校时可用。
// lwip2 默认开启 SNTP_MONITOR_SERVER_REACHABILITY，返回最近 8 次轮询的响应位图。
static bool sntpReachable() {
    for (uint8_t i = 0; i < 3; i++) {
        if (sntp_getreachability(i) != 0) {
            return true;
        }
    }
    return false;
}

void TimeManager::init() {
    source_ = SRC_NONE;
    ntp_session_ok_ = false;
    last_sync_unix_ = 0;
    last_sync_millis_ = 0;
    last_ntp_request_millis_ = 0;
    ntp_requested_ = false;

    // 时区与是否联网无关：保证 AP 模式下浏览器校时后 localtime() 也是北京时间
    setenv("TZ", "CST-8", 1);
    tzset();

    DBG_PRINTF("[Time] init: no persisted clock, waiting for NTP or browser sync\n");
}

void TimeManager::handle() {
    static unsigned long last_check = 0;
    unsigned long ms = millis();
    if (ms - last_check < 1000) return;
    last_check = ms;

    bool sta = (WiFi.status() == WL_CONNECTED);
    bool valid = isValid();

    if (!sta) {
        if (ntp_session_ok_) {
            ntp_session_ok_ = false;
            DBG_PRINTF("[Time] offline: local clock keeps running, browser sync allowed\n");
        }
        return;
    }

    if (ntp_session_ok_) return;

    if (valid && sntpReachable()) {
        ntp_session_ok_ = true;
        source_ = SRC_NTP;
        last_sync_unix_ = (uint32_t)time(nullptr);
        last_sync_millis_ = ms;
        DBG_PRINTF("[Time] NTP synced, source=NTP\n");
        return;
    }

    // 首次同步失败（DNS 未就绪 / 服务器不可达）时按固定间隔重试
    if (!ntp_requested_ || ms - last_ntp_request_millis_ >= NTP_RESYNC_RETRY_MS) {
        requestNtpSync();
    }
}

void TimeManager::requestNtpSync() {
    configTime(TIME_TZ_OFFSET_SEC, 0, NTP_SERVER_1, NTP_SERVER_2, NTP_SERVER_3);
    ntp_requested_ = true;
    last_ntp_request_millis_ = millis();
    DBG_PRINTF("[Time] NTP request sent (%s)\n", NTP_SERVER_1);
}

void TimeManager::onNetworkUp() {
    ntp_session_ok_ = false;
    ntp_requested_ = false;
    requestNtpSync();
}

void TimeManager::onNetworkDown() {
    if (ntp_session_ok_) {
        DBG_PRINTF("[Time] network down, local clock continues from %lu\n", (unsigned long)time(nullptr));
    }
    ntp_session_ok_ = false;
}

bool TimeManager::applyBrowserTime(time_t ts, const char** reason) {
    static const char* unused = "";
    if (!reason) reason = &unused;

    if ((uint32_t)ts < TIME_VALID_MIN_UNIX) {
        *reason = "invalid";
        return false;
    }

    // 优先级：联网且 NTP 已同步时，浏览器时间不得覆盖
    if (ntpAvailable()) {
        *reason = "ntp";
        DBG_PRINTF("[Time] browser sync rejected: NTP is authoritative\n");
        return false;
    }

    struct timeval tv = { .tv_sec = ts, .tv_usec = 0 };
    settimeofday(&tv, nullptr);
    source_ = SRC_BROWSER;
    last_sync_unix_ = (uint32_t)ts;
    last_sync_millis_ = millis();
    *reason = "browser";
    DBG_PRINTF("[Time] browser sync applied: %lu\n", (unsigned long)ts);
    return true;
}

bool TimeManager::isValid() {
    return ((uint32_t)time(nullptr) >= TIME_VALID_MIN_UNIX);
}

time_t TimeManager::now() {
    // core 的 time() = micros64()/1e6 + timeshift：
    // 校时后即使断网也持续走时，掉电后 micros64 归零即回到"未校时"。
    return time(nullptr);
}

TimeManager::Source TimeManager::source() {
    return source_;
}

uint32_t TimeManager::syncAgeSec() {
    if (last_sync_millis_ == 0) return 0;
    return (uint32_t)((millis() - last_sync_millis_) / 1000UL);
}

uint32_t TimeManager::lastSyncUnix() {
    return last_sync_unix_;
}

bool TimeManager::ntpAvailable() {
    return (WiFi.status() == WL_CONNECTED) && ntp_session_ok_ && isValid();
}
