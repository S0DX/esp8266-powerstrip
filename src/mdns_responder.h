#ifndef MDNS_RESPONDER_H
#define MDNS_RESPONDER_H

#include <Arduino.h>
#include <WiFiUdp.h>

/**
 * 轻量 mDNS responder：只做一件事——应答 {hostname}.local 的 A 记录查询。
 *
 * 为什么不用 core 的 ESP8266mDNS：
 *  1. 它在 AP_STA 双模下绑定接口不稳定（wifi_mgr 里为此做过切模式 hack，仍然不可靠）；
 *  2. 纯 AP 模式下它 advertise 的是 station IP，AP 客户端解析不到；
 *  3. modem sleep 会丢 5353 组播查询包。
 *
 * 本实现同时监听 STA 与 AP 两个接口的 224.0.0.251:5353 组播，按查询到达的
 * 接口回对应的 A 记录（STA 查询回 STA IP，AP 查询回 192.168.4.1），
 * 单播直接回给查询者，AP 与局域网两种场景都能解析 power.local。
 * 注意：responder 运行期间 WiFi 必须保持 NONE_SLEEP，由 updateSleepMode 协调。
 */
class MDNSResponder {
public:
    // 绑定两个接口的组播并记录 hostname（取自 WiFiManager::getHostname()）
    static void start();
    // 主循环调用：收查询、回响应；每 10s 检查 IP/模式变化自动重绑
    static void handle();
    static bool running();
    static void stop();

private:
    static WiFiUDP udp_sta_;
    static WiFiUDP udp_ap_;
    static WiFiUDP udp_llmnr_;
    static IPAddress sta_ip_;
    static IPAddress ap_ip_;
    static char hostname_[13];   // 最长 10 字符（EEPROM_HOSTNAME_MAX_LEN）+ NUL
    static bool started_;
    static bool sta_ok_;
    static bool ap_ok_;
    static unsigned long last_rebind_ms_;

    static void rebind();
    // 解析并应答单个实例上收到的查询；isMdns=true 匹配 <hostname>.local，
    // false 匹配单标签 <hostname>（LLMNR，Windows）
    static void process(WiFiUDP& udp, const IPAddress& ifIp, bool isAp, bool isMdns);
    // 在报文中定位目标名字，返回起始偏移，未找到返回 -1
    static int findName(const uint8_t* buf, int len, int* nameLen, bool withLocal);
    // 构造并发送 A 记录响应（mDNS 组播+单播双发；LLMNR 单播）
    static void sendResponse(WiFiUDP& udp, const uint8_t* qname, int qnameLen,
                             const IPAddress& ifIp, bool isMdns, uint16_t queryId,
                             uint16_t qType, uint16_t qClass,
                             const uint8_t* questions, int qLen, int qdcount);
    // AAAA 查询的空回答（NOERROR + ANCOUNT=0）：Android 解析器会先问 AAAA，
    // 无答复会判解析失败；规范的空响应让它回退去问 A
    static void sendEmptyResponse(WiFiUDP& udp, const IPAddress& ifIp, bool isMdns,
                                  uint16_t queryId, const uint8_t* questions, int qLen, int qdcount);
};

#endif
