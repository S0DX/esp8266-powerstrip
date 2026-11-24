#ifndef UPDATE_CHECKER_H
#define UPDATE_CHECKER_H

#include <Arduino.h>

/**
 * 自动检测更新（v5.9）
 *
 * L1 检测提醒：每 UPDATE_CHECK_INTERVAL_MS（默认 1 周）+ 开机 1 分钟首查，
 *   HTTPS GET jsdelivr 上的 version.json 清单，与 VERSION 做语义比较，
 *   发现新版本置 available_ 并透出给 /api/status（uav/ulv 字段）。
 * L2 在线升级：doUpgrade() 用 ESP8266httpUpdate 直连清单里的 bin 直链刷写，
 *   成功自动重启，失败返回 false。仅由 Web 端"在线升级"按钮显式触发。
 *
 * 安全与可用性边界（README 有同步说明）：
 *  - TLS 用 setInsecure 跳过证书校验（ESP8266 内存不足以做完整 CA 链校验），
 *    清单固定指向本项目 jsdelivr/release 地址；运行时 sha256 校验不做，
 *    依赖 TLS 传输 + httpUpdate 分区校验 + 失败回滚。
 *  - 检测阻塞主循环最长 ~8s（每 7 天一次），期间 Web 响应会排队。
 *  - 清单与 bin 均走 CDN/GitHub，国内可达性以 jsdelivr 较好，失败静默重试。
 */
class UpdateChecker {
public:
    static void init();
    static void handle();
    // 手动立即检查（同步，最长 ~8s）。返回检查是否成功执行（不代表有新版本）
    static bool checkNow();
    static bool available();
    static String latestVersion();
    static String lastError();
    // L2：在线升级（阻塞，成功后设备自动重启，函数不返回）
    static bool doUpgrade();

private:
    static bool checkInternal(String& outVer, String& outBin);
    static bool extractField(const String& body, const char* key, String& out);
    static bool semverGreater(const String& candidate, const String& current);
    static unsigned long last_check_ms_;
    static bool first_check_done_;
    static bool available_;
    static String latest_ver_;
    static String bin_url_;
    static String last_error_;
};

#endif
