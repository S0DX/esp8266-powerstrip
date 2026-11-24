#include "mdns_responder.h"
#include "wifi_mgr.h"
#include <ESP8266WiFi.h>

// mDNS 组播地址与端口（RFC 6762）；LLMNR 组播地址与端口（RFC 4795，Windows 原生）
static const IPAddress MDNS_ADDR(224, 0, 0, 251);
static const uint16_t MDNS_PORT = 5353;
static const IPAddress LLMNR_ADDR(224, 0, 0, 252);
static const uint16_t LLMNR_PORT = 5355;

WiFiUDP MDNSResponder::udp_sta_;
WiFiUDP MDNSResponder::udp_ap_;
WiFiUDP MDNSResponder::udp_llmnr_;
IPAddress MDNSResponder::sta_ip_;
IPAddress MDNSResponder::ap_ip_;
char MDNSResponder::hostname_[13] = "power";
bool MDNSResponder::started_ = false;
bool MDNSResponder::sta_ok_ = false;
bool MDNSResponder::ap_ok_ = false;
unsigned long MDNSResponder::last_rebind_ms_ = 0;

void MDNSResponder::start() {
    String h = WiFiManager::getHostname();
    strncpy(hostname_, h.c_str(), sizeof(hostname_) - 1);
    hostname_[sizeof(hostname_) - 1] = '\0';
    started_ = true;
    rebind();
    Serial.printf("[mDNS] responder started for %s.local / %s (LLMNR) (sta=%s ap=%s)\n",
        hostname_, hostname_, sta_ok_ ? "on" : "off", ap_ok_ ? "on" : "off");
}

void MDNSResponder::stop() {
    udp_sta_.stop();
    udp_ap_.stop();
    started_ = false;
    sta_ok_ = false;
    ap_ok_ = false;
    Serial.println("[mDNS] responder stopped");
}

bool MDNSResponder::running() {
    return started_;
}

void MDNSResponder::rebind() {
    // 全量重绑：先关所有 socket，再按当前接口状态重建（AP 关闭后旧绑定一并清理）
    udp_sta_.stop();
    udp_ap_.stop();
    udp_llmnr_.stop();
    sta_ok_ = false;
    ap_ok_ = false;

    // STA 接口：拿到 DHCP IP 才能绑定
    IPAddress s = WiFi.localIP();
    uint32_t sRaw = (uint32_t)s;
    if (sRaw != 0) {
        sta_ok_ = udp_sta_.beginMulticast(s, MDNS_ADDR, MDNS_PORT);
        // LLMNR：同一 STA 接口，Windows 原生名字解析（单标签名，组播 224.0.0.252:5355）
        udp_llmnr_.beginMulticast(s, LLMNR_ADDR, LLMNR_PORT);
        sta_ip_ = s;
    }

    // AP 接口：AP 开启即绑定
    if (WiFi.getMode() & WIFI_AP) {
        IPAddress a = WiFi.softAPIP();
        uint32_t aRaw = (uint32_t)a;
        if (aRaw != 0) {
            udp_ap_.stop();
            ap_ok_ = udp_ap_.beginMulticast(a, MDNS_ADDR, MDNS_PORT);
            ap_ip_ = a;
        }
    }

    // 组播监听不能工作在 modem sleep（会丢 5353/5355 查询包），强制常醒
    WiFi.setSleepMode(WIFI_NONE_SLEEP);
}

void MDNSResponder::handle() {
    if (!started_) return;

    // 每 10s 检查接口变化（DHCP 换 IP / AP 重建 / 模式切换）并重绑
    if (millis() - last_rebind_ms_ >= 10000) {
        last_rebind_ms_ = millis();
        IPAddress s = WiFi.localIP();
        bool needRebind = ((uint32_t)s != (uint32_t)sta_ip_) ||
                          ((WiFi.getMode() & WIFI_AP) && (uint32_t)WiFi.softAPIP() != (uint32_t)ap_ip_);
        if (needRebind) rebind();
    }

    if (sta_ok_) {
        process(udp_sta_, sta_ip_, false, true);   // mDNS：<hostname>.local
        process(udp_llmnr_, sta_ip_, false, false); // LLMNR：<hostname>（Windows）
    }
    if (ap_ok_) process(udp_ap_, ap_ip_, true, true);
}

// 在报文中定位 DNS 编码的 "<hostname>.local"（大小写不敏感）
// 返回名字起始偏移（应为 12，即 header 之后），nameLen 输出名字总长（含结尾 0x00）
int MDNSResponder::findName(const uint8_t* buf, int len, int* nameLen, bool withLocal) {
    // 构造 DNS 名字编码：[len]hostname（+ [0x05]"local"）[0x00]
    uint8_t target[20];
    int hlen = strlen(hostname_);
    if (hlen <= 0 || hlen > 10) return -1;
    int tl = 0;
    target[tl++] = (uint8_t)hlen;
    for (int i = 0; i < hlen; i++) {
        target[tl++] = (uint8_t)hostname_[i];
    }
    if (withLocal) {
        target[tl++] = 5;
        target[tl++] = 'l'; target[tl++] = 'o'; target[tl++] = 'c'; target[tl++] = 'a'; target[tl++] = 'l';
    }
    target[tl++] = 0;

    // QNAME 从 header（12B）之后开始，向后再扫一点兼容压缩指针之外的少见布局
    for (int pos = 12; pos + tl <= len; pos++) {
        bool match = true;
        for (int i = 0; i < tl; i++) {
            if (tolower((int)buf[pos + i]) != tolower((int)target[i])) {
                match = false;
                break;
            }
        }
        if (match) {
            *nameLen = tl;
            return pos;
        }
    }
    return -1;
}

void MDNSResponder::sendResponse(WiFiUDP& udp, const uint8_t* qname, int qnameLen,
                                 const IPAddress& ifIp, bool isMdns) {
    int n = qnameLen;
    if (n < 0 || n > 240) return;

    uint8_t r[280];
    memset(r, 0, sizeof(r));
    r[2] = 0x84; r[3] = 0x00;                 // QR=1, AA=1
    r[4] = 0;    r[5] = 1;                    // QDCOUNT=1
    r[6] = 0;    r[7] = 1;                    // ANCOUNT=1
    memcpy(r + 12, qname, n);                 // question 名字（原样拷贝，保持大小写）
    r[12 + n] = 0;     r[12 + n + 1] = 1;     // QTYPE=A
    r[12 + n + 2] = 0; r[12 + n + 3] = 1;     // QCLASS=IN
    r[12 + n + 4] = 0xC0; r[12 + n + 5] = 0x0C; // answer 名字压缩指针 -> offset 12
    r[12 + n + 6] = 0;    r[12 + n + 7] = 1;  // TYPE=A
    r[12 + n + 8] = 0x80; r[12 + n + 9] = 0x01; // CLASS=IN + cache-flush
    uint32_t ttl = isMdns ? 120 : 30;         // LLMNR 用短 TTL（RFC 4795 建议 30s）
    r[12 + n + 10] = (uint8_t)(ttl >> 24);
    r[12 + n + 11] = (uint8_t)(ttl >> 16);
    r[12 + n + 12] = (uint8_t)(ttl >> 8);
    r[12 + n + 13] = (uint8_t)ttl;
    r[12 + n + 14] = 0; r[12 + n + 15] = 4;   // RDLENGTH=4
    uint32_t ip = (uint32_t)ifIp;
    memcpy(r + 12 + n + 16, &ip, 4);
    int total = 12 + n + 20;

    // mDNS 标准查询（源端口 5353、ID=0）期望组播应答——只发单播会被
    // Windows/iOS 解析器忽略；legacy 查询（源端口随机）期望单播应答。
    // 两种都发，重复无害。LLMNR 规范允许单播应答。
    if (isMdns) {
        udp.beginPacketMulticast(MDNS_ADDR, MDNS_PORT, ifIp);
        udp.write(r, total);
        udp.endPacket();
    }
    IPAddress rip = udp.remoteIP();
    uint16_t rport = udp.remotePort();
    if (rport == 0) return;
    udp.beginPacket(rip, rport);
    udp.write(r, total);
    udp.endPacket();

    Serial.printf("[mDNS] %s%s -> %s (%s, query from %s:%u)\n",
        hostname_, isMdns ? ".local" : " (LLMNR)", ifIp.toString().c_str(),
        isMdns ? "mDNS" : "LLMNR",
        rip.toString().c_str(), rport);
}

void MDNSResponder::process(WiFiUDP& udp, const IPAddress& ifIp, bool isAp, bool isMdns) {
    int len = udp.parsePacket();
    if (len <= 0) return;

    // 来源网段过滤：AP_STA 双模下 lwip 会把局域网来的组播同时投递给两个已
    // join 组播的 socket，若两个实例都应答，查询者会收到 192.168.0.x 与
    // 192.168.4.1 两个 A 记录并可能采用不可路由的 AP IP（实测踩坑）。
    // 规则：AP 实例只应答 AP 子网（与 softAPIP 同 /24）来源，STA 实例只应答其余。
    IPAddress rip = udp.remoteIP();
    IPAddress apIp = WiFi.softAPIP();
    bool ripInApLan = (rip[0] == apIp[0] && rip[1] == apIp[1] && rip[2] == apIp[2]);
    if (isAp != ripInApLan) return;

    uint8_t buf[256];
    if (len > (int)sizeof(buf)) len = sizeof(buf);
    udp.read(buf, len);

    if (len < 18) return;
    // 只处理查询（QR=0），不响应遗留响应/通知
    if (buf[2] & 0x80) return;
    uint16_t qdcount = ((uint16_t)buf[4] << 8) | buf[5];
    if (qdcount < 1) return;

    int nameLen = 0;
    int pos = findName(buf, len, &nameLen, isMdns);
    if (pos < 0) return;

    // 名字之后的 QTYPE/QCLASS
    int off = pos + nameLen;
    if (off + 4 > len) return;
    uint16_t qtype = ((uint16_t)buf[off] << 8) | buf[off + 1];
    // 只应答 A(1) 与 ANY(255) 查询
    if (qtype != 1 && qtype != 255) return;

    // 响应里的 question 名字直接用查询报文中匹配到的完整名字（原样拷贝）
    sendResponse(udp, buf + pos, nameLen, ifIp, isMdns);
}
