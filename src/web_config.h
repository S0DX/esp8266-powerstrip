#ifndef WEB_CONFIG_H
#define WEB_CONFIG_H

#include <ESP8266WebServer.h>
#include <memory>

class WebConfigServer {
public:
    static void init();
    static void handle();
    static void setSaveCallback(void (*callback)(const char* ssid, const char* pass));

    // P0: Web 请求优先级机制
    // 主循环检测到此标志时跳过 SY7T609 读取等阻塞操作，避免 TCP ACK 超时
    static bool isWebRequestActive();

private:
    static std::unique_ptr<ESP8266WebServer> server_;
    static void (*save_callback_)(const char*, const char*);
    // P0: Web 请求处理中标志（带 50ms 超时保护）
    static volatile bool web_request_active_;
    static unsigned long web_request_start_;
    static void markWebRequestStart();
    static void markWebRequestEnd();
    // P0: 判断当前 URI 是否为关键 API（扫描期间仍需响应）
    static bool isCriticalApi(const String& uri);
};

#endif