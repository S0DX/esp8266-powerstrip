#include "web_config.h"
#include "wifi_mgr.h"
#include "gpio_mgr.h"
#include "config.h"
#include "energy_mgr.h"
#include "log_buffer.h"
#include "index_html_gz.h"
#include <sys/time.h>
#include "sy7t609.h"
#include "mqtt_mgr.h"
#include <EEPROM.h>
#include <WiFiClient.h>
#include <ESP8266HTTPUpdateServer.h>
#include <ESP8266httpUpdate.h>
#include <StreamString.h>
#include <DNSServer.h>

std::unique_ptr<ESP8266WebServer> WebConfigServer::server_;
std::unique_ptr<ESP8266HTTPUpdateServer> httpUpdater_;
std::unique_ptr<DNSServer> dnsServer_;
void (*WebConfigServer::save_callback_)(const char*, const char*) = nullptr;

// P0: Web 请求优先级机制
volatile bool WebConfigServer::web_request_active_ = false;
unsigned long WebConfigServer::web_request_start_ = 0;

// /api/status 使用静态缓冲区，避免 1600 字节大数组压在栈上导致栈溢出
static char status_buf[2048];

// P0: Web 请求优先级机制实现
bool WebConfigServer::isWebRequestActive() {
    // 超时保护：50ms 后自动清除（防止死锁导致主循环永久跳过 SY7T609 读取）
    if (web_request_active_ && millis() - web_request_start_ > 50) {
        web_request_active_ = false;
    }
    return web_request_active_;
}

void WebConfigServer::markWebRequestStart() {
    web_request_active_ = true;
    web_request_start_ = millis();
}

void WebConfigServer::markWebRequestEnd() {
    web_request_active_ = false;
}

// P0: 关键 API 列表（WiFi 扫描期间仍需响应）
bool WebConfigServer::isCriticalApi(const String& uri) {
    return uri == "/" || uri == "/api/status" || uri == "/api/debug" ||
           uri == "/api/meter" || uri == "/api/relay" ||
           uri.startsWith("/api/connect") || uri == "/api/now";
}


void WebConfigServer::init() {
    server_ = std::make_unique<ESP8266WebServer>(WEB_SERVER_PORT);
    httpUpdater_ = std::make_unique<ESP8266HTTPUpdateServer>();
    dnsServer_ = std::make_unique<DNSServer>();

    httpUpdater_->setup(server_.get(), "/update", "admin", "admin");

    // 启动 DNS 服务器，实现 Captive Portal (强制门户) 拦截所有域名解析请求重定向到 ESP
    dnsServer_->start(53, "*", WiFi.softAPIP());

    server_->on("/", HTTP_GET, []() {
        WiFiManager::boostTxPower();  // P1: 页面访问时临时提升功率
        server_->sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
        server_->sendHeader("Pragma", "no-cache");
        server_->sendHeader("Expires", "-1");
        server_->sendHeader("Content-Encoding", "gzip");
        server_->setContentLength(sizeof(INDEX_HTML_GZ));
        server_->send(200, "text/html; charset=UTF-8", "");
        server_->sendContent_P((const char*)INDEX_HTML_GZ, sizeof(INDEX_HTML_GZ));
        // 发送完成后让出 1ms，让 lwIP/TCP 栈有机会把数据真正推出去，
        // 避免在弱信号或慢客户端场景下因缓冲区未排空导致页面空白/截断。
        delay(1);
    });

    // iOS Captive Portal 检测端点：返回 200 HTML 页面并自动跳转到配置页。
    // 若返回 "Success" 或 302 重定向，iOS 某些版本会判定已联网而不弹出门户。
    server_->on("/hotspot-detect.html", HTTP_GET, []() {
        server_->sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
        server_->sendHeader("Pragma", "no-cache");
        server_->sendHeader("Expires", "-1");
        String portalUrl = String("http://") + WiFi.softAPIP().toString() + "/";
        String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'>";
        html += "<meta http-equiv='refresh' content='0; url=" + portalUrl + "'>";
        html += "<title>WiFi 配置</title></head><body>";
        html += "<p style='font-family:-apple-system,sans-serif;text-align:center;padding-top:40px'>";
        html += "请打开 <a href='" + portalUrl + "'>配置页面</a>";
        html += "</p></body></html>";
        server_->send(200, "text/html; charset=UTF-8", html);
    });

    // Android Captive Portal 检测：期望 204，返回 302 让 Android 判定为门户并弹窗
    // 覆盖 clients3.google.com / connectivitycheck.gstatic.com 等变体的探测路径
    server_->on("/generate_204", HTTP_GET, []() {
        String portalUrl = String("http://") + WiFi.softAPIP().toString() + "/";
        server_->sendHeader("Location", portalUrl, true);
        server_->send(302, "text/plain", "");
    });
    server_->on("/gen_204", HTTP_GET, []() {
        String portalUrl = String("http://") + WiFi.softAPIP().toString() + "/";
        server_->sendHeader("Location", portalUrl, true);
        server_->send(302, "text/plain", "");
    });

    // Windows Captive Portal 检测：msftconnecttest.com/connecttest.txt 与 /redirect
    server_->on("/connecttest.txt", HTTP_GET, []() {
        String portalUrl = String("http://") + WiFi.softAPIP().toString() + "/";
        server_->sendHeader("Location", portalUrl, true);
        server_->send(302, "text/plain", "");
    });
    server_->on("/redirect", HTTP_GET, []() {
        String portalUrl = String("http://") + WiFi.softAPIP().toString() + "/";
        server_->sendHeader("Location", portalUrl, true);
        server_->send(302, "text/plain", "");
    });

    server_->on("/api/status", HTTP_GET, []() {
        WiFiManager::boostTxPower();  // P1: 状态查询时临时提升功率
        String ip = (WiFi.status() == WL_CONNECTED) ? WiFi.localIP().toString() : WiFi.softAPIP().toString();
        uint8_t sh, sm, eh, em;
        GPIOManager::getCycleTime(sh, sm, eh, em);
        MQTTConfig mq = MQTTManager::getConfig();
        String apPass = WiFiManager::getAPPassword();

        time_t nowTs = time(nullptr);
        struct tm* tinfo = localtime(&nowTs);
        bool timeValid = (tinfo && tinfo->tm_year > 100);
        int curMins = timeValid ? (tinfo->tm_hour * 60 + tinfo->tm_min) : -1;

        snprintf(status_buf, sizeof(status_buf),
            "{\"conn\":%s,\"ip\":\"%s\",\"ssid\":\"%s\",\"rssi\":%d,"
            "\"ver\":\"%s\",\"up\":%lu,\"m\":%s,\"s\":%s,"
            "\"lk\":%s,\"te\":%s,\"tr\":%s,\"td\":%d,\"tl\":%lu,"
            "\"v\":%.1f,\"i\":%.3f,\"p\":%.2f,\"e\":%.2f,\"me\":%s,\"rl\":%s,"
            "\"ce\":%s,\"sh\":%d,\"sm\":%d,\"eh\":%d,\"em\":%d,\"cpc\":%d,\"ca\":%s,\"tv\":%s,\"cm\":%d,"
            "\"pe\":%s,\"pt\":%.2f,"
            "\"be\":%s,\"bt\":%.2f,\"bm\":%.2f,\"btt\":%d,\"bmo\":%d,\"bu\":%.2f,\"bum\":%.2f,\"but\":%u,\"bsr\":%d,\"ep\":%.2f,\"mo\":%.2f,\"lm\":%.2f,"
            "\"de\":%s,\"dt\":\"%s\",\"dl\":%s,\"dr\":%d,\"dm\":\"%s\",\"dp\":%s,\"pc\":%s,\"ds\":%d,\"ts\":%d,\"om\":%s,\"bd\":%s,"
            "\"mqe\":%s,\"mqs\":\"%s\",\"mqp\":%d,\"mqu\":\"%s\",\"mqpw\":\"%s\","
            "\"ap\":\"%s\",\"aps\":\"%s\",\"host\":\"%s\",\"cv\":%d,\"bat\":%s,\"acap\":%s,\"wtpl\":%s,\"syslog\":%s,\"rst\":\"%s\"}",
            WiFiManager::isConnected() ? "true" : "false",
            ip.c_str(),
            WiFiManager::getCurrentSSID().c_str(),
            WiFiManager::getCurrentRSSI(),
            VERSION,
            millis() / 1000,
            GPIOManager::getRelayMaster() ? "true" : "false",
            GPIOManager::getRelaySlave() ? "true" : "false",
            GPIOManager::isLocked() ? "true" : "false",
            GPIOManager::isTimerEnabled() ? "true" : "false",
            GPIOManager::isTimerRunning() ? "true" : "false",
            GPIOManager::getTimerDuration(),
            GPIOManager::getTimerRemaining(),
            SY7T609::getVoltage(),
            SY7T609::getCurrent(),
            SY7T609::getPower(),
            EnergyManager::getTotalEnergy(),
            SY7T609::isEnabled() ? "true" : "false",
            GPIOManager::isRedLedEnabled() ? "true" : "false",
            GPIOManager::isCycleEnabled() ? "true" : "false",
            sh, sm, eh, em, GPIOManager::getCyclePeriodCount(),
            GPIOManager::isCycleActive() ? "true" : "false",
            timeValid ? "true" : "false",
            curMins,
            GPIOManager::isPowerOffEnabled() ? "true" : "false",
            GPIOManager::getPowerOffThreshold(),
            GPIOManager::isBillingEnabled() ? "true" : "false",
            GPIOManager::getBillingThreshold(),
            GPIOManager::getBillingThresholdMoney(),
            GPIOManager::getBillingThresholdTime(),
            (int)GPIOManager::getBillingMode(),
            GPIOManager::getBillingUsedEnergy() / 1000.0f,
            GPIOManager::getBillingUsedMoney(),
            GPIOManager::getBillingUsedMinutes(),
            (int)GPIOManager::getBillingStopReason(),
            GPIOManager::getEnergyPrice(),
            EnergyManager::getMonthlyEnergy() / 1000.0f,
            EnergyManager::getLastMonthEnergy() / 1000.0f,
            GPIOManager::isWiFiDetectEnabled() ? "true" : "false",
            GPIOManager::getWiFiDetectTarget().c_str(),
            GPIOManager::isWiFiDetectLinkTimer() ? "true" : "false",
            GPIOManager::getWiFiDetectRssiThreshold(),
            GPIOManager::getWiFiDetectMac().c_str(),
            GPIOManager::isWiFiDetectPresent() ? "true" : "false",
            GPIOManager::isWiFiDetectPowerChecking() ? "true" : "false",
            GPIOManager::getWiFiDetectScanSpeed(),
            GPIOManager::getTimerSource(),
            GPIOManager::getWiFiDetectRelayOnlyMaster() ? "true" : "false",
            GPIOManager::isButtonDetectEnabled() ? "true" : "false",
            mq.enabled ? "true" : "false",
            mq.server,
            mq.port,
            mq.username,
            mq.password,
            apPass.c_str(),
            WiFiManager::getAPSuffix().c_str(),
            WiFiManager::getHostname().c_str(),
            EEPROM.read(EEPROM_CARD_VISIBILITY_ADDR),
            GPIOManager::isButtonAutoTimerEnabled() ? "true" : "false",
            WiFiManager::isAutoCloseAPEnabled() ? "true" : "false",
            WiFiManager::isWiFiTxPowerLimited() ? "true" : "false",
            log_buffer_is_enabled() ? "true" : "false",
            ESP.getResetReason().c_str()
        );
        int len = strlen(status_buf);
        uint8_t cpCount = GPIOManager::getCyclePeriodCount();
        if (len > 0 && status_buf[len-1] == '}') {
            status_buf[--len] = '\0';
            len += snprintf(status_buf + len, sizeof(status_buf) - len, ",\"cp\":[");
            for (uint8_t i = 0; i < cpCount && len < (int)sizeof(status_buf) - 40; i++) {
                uint8_t psh,psm,peh,pem;
                if (GPIOManager::getCyclePeriod(i, psh, psm, peh, pem)) {
                    if (i > 0 && len < (int)sizeof(status_buf) - 1) status_buf[len++] = ',';
                    len += snprintf(status_buf + len, sizeof(status_buf) - len,
                        "{\"sh\":%d,\"sm\":%d,\"eh\":%d,\"em\":%d}", psh, psm, peh, pem);
                }
            }
            if (len < (int)sizeof(status_buf) - 20) {
                len += snprintf(status_buf + len, sizeof(status_buf) - len, "],\"card_order\":[");
                for (uint8_t i = 0; i < 6 && len < (int)sizeof(status_buf) - 10; i++) {
                    if (i > 0 && len < (int)sizeof(status_buf) - 1) status_buf[len++] = ',';
                    uint8_t v = EEPROM.read(EEPROM_CARD_ORDER_ADDR + i);
                    if (v > 5) v = i;
                    len += snprintf(status_buf + len, sizeof(status_buf) - len, "%d", v);
                }
                if (len < (int)sizeof(status_buf) - 2) {
                    len += snprintf(status_buf + len, sizeof(status_buf) - len, "]}");
                }
            }
        }
        server_->send(200, "application/json", status_buf);
    });


    // Debug API for SY7T609
    server_->on("/api/debug", HTTP_GET, []() {
        String json = "{\"log\":";
        json += "\"" + SY7T609::getDebugLog() + "\"";
        json += ",\"meterEnabled\":" + String(SY7T609::isEnabled() ? "true" : "false");
        json += ",\"meterReady\":" + String(SY7T609::isReady() ? "true" : "false");
        json += ",\"flashMode\":" + String(SY7T609::isFlashMode() ? "true" : "false");
        json += "}";
        server_->send(200, "application/json", json);
    });

    server_->on("/api/cleardebug", HTTP_POST, []() {
        SY7T609::clearDebugLog();
        server_->send(200, "application/json", "{\"ok\":true}");
    });

    server_->on("/api/relay", HTTP_GET, []() {
        String w = server_->arg("w");
        if (w == "m") {
            bool newState = !GPIOManager::getRelayMaster();
            GPIOManager::setRelayMaster(newState);
            // 关闭主继电器时，同时关闭从继电器
            if (!newState) {
                GPIOManager::setRelaySlave(false);
            }
        } else if (w == "s") {
            bool newState = !GPIOManager::getRelaySlave();
            GPIOManager::setRelaySlave(newState);
            // 开启从继电器时，同时开启主继电器
            if (newState) {
                GPIOManager::setRelayMaster(true);
            }
        }
        String response = "{\"m\":" + String(GPIOManager::getRelayMaster() ? "true" : "false") +
                          ",\"s\":" + String(GPIOManager::getRelaySlave() ? "true" : "false") + "}";
        server_->send(200, "application/json", response);
    });

    server_->on("/api/cycle", HTTP_GET, []() {
        if (server_->hasArg("enabled")) {
            bool on = (server_->arg("enabled") == "true");
            GPIOManager::setCycleEnabled(on);
            server_->send(200, "application/json", "{\"enabled\":" + String(on ? "true" : "false") + "}");
            return;
        }
        if (server_->hasArg("add")) {
            uint8_t sh = server_->arg("sh").toInt();
            uint8_t sm = server_->arg("sm").toInt();
            uint8_t eh = server_->arg("eh").toInt();
            uint8_t em = server_->arg("em").toInt();
            if (sh > 23 || sm > 59 || eh > 23 || em > 59 || (sh == eh && sm == em)) {
                server_->send(200, "application/json", "{\"ok\":false,\"error\":\"invalid period\"}");
                return;
            }
            bool ok = GPIOManager::addCyclePeriod(sh, sm, eh, em);
            server_->send(200, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false,\"error\":\"max periods\"}");
            return;
        }
        if (server_->hasArg("remove")) {
            uint8_t idx = server_->arg("remove").toInt();
            bool ok = GPIOManager::removeCyclePeriod(idx);
            server_->send(200, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false,\"error\":\"invalid index\"}");
            return;
        }
        if (server_->hasArg("sh") && server_->hasArg("eh")) {
            uint8_t sh = server_->arg("sh").toInt();
            uint8_t sm = server_->arg("sm").toInt();
            uint8_t eh = server_->arg("eh").toInt();
            uint8_t em = server_->arg("em").toInt();
            if (sh > 23 || sm > 59 || eh > 23 || em > 59 || (sh == eh && sm == em)) {
                server_->send(200, "application/json", "{\"ok\":false,\"error\":\"invalid period\"}");
                return;
            }
            uint8_t idx = server_->hasArg("idx") ? server_->arg("idx").toInt() : 0;
            GPIOManager::setCyclePeriod(idx, sh, sm, eh, em);
            server_->send(200, "application/json", "{\"ok\":true}");
            return;
        }
        server_->send(400, "application/json", "{\"error\":\"bad request\"}");
    });

    server_->on("/api/mqtt", HTTP_POST, []() {
        MQTTConfig conf = MQTTManager::getConfig();
        if (server_->hasArg("enabled")) {
            conf.enabled = (server_->arg("enabled") == "true");
            MQTTManager::saveConfig(conf);
            server_->send(200, "application/json", "{\"enabled\":" + String(conf.enabled ? "true" : "false") + "}");
            return;
        }
        if (server_->hasArg("server")) {
            strncpy(conf.server, server_->arg("server").c_str(), sizeof(conf.server)-1);
            conf.server[sizeof(conf.server)-1] = '\0';
            conf.port = (uint16_t)server_->arg("port").toInt();
            strncpy(conf.username, server_->arg("user").c_str(), sizeof(conf.username)-1);
            conf.username[sizeof(conf.username)-1] = '\0';
            strncpy(conf.password, server_->arg("pass").c_str(), sizeof(conf.password)-1);
            conf.password[sizeof(conf.password)-1] = '\0';
            MQTTManager::saveConfig(conf);
            server_->send(200, "application/json", "{\"ok\":true}");
            return;
        }
        server_->send(400, "application/json", "{\"error\":\"bad request\"}");
    });


    server_->on("/api/lock", HTTP_GET, []() {
        bool current = GPIOManager::isLocked();
        GPIOManager::setLocked(!current);
        String response = "{\"locked\":" + String(GPIOManager::isLocked() ? "true" : "false") + "}";
        server_->send(200, "application/json", response);
    });

    // 定时器 API
    server_->on("/api/timer", HTTP_GET, []() {
        String enabled = server_->arg("enabled");
        String duration = server_->arg("duration");

        if (duration.length() > 0) {
            uint16_t mins = duration.toInt();
            GPIOManager::setTimerDuration(mins);
        }

        if (enabled.length() > 0) {
            bool en = (enabled == "true");
            GPIOManager::setTimerEnabled(en);
            if (en) {
                GPIOManager::startTimer();
            } else {
                GPIOManager::stopTimer();
            }
            String response = "{\"enabled\":" + String(en ? "true" : "false") + ",\"duration\":" + String(GPIOManager::getTimerDuration()) + "}";
            server_->send(200, "application/json", response);
        } else if (duration.length() > 0) {
            String response = "{\"duration\":" + String(GPIOManager::getTimerDuration()) + "}";
            server_->send(200, "application/json", response);
        } else {
            server_->send(400, "application/json", "{\"error\":\"Invalid parameters\"}");
        }
    });

    server_->on("/api/card_order", HTTP_GET, []() {
        uint8_t order[6] = {0,1,2,3,4,5};
        for(int i=0;i<6;i++) order[i] = EEPROM.read(EEPROM_CARD_ORDER_ADDR+i);
        bool dup = false;
        for(int i=0;i<6 && !dup;i++) for(int j=i+1;j<6;j++) if(order[i]==order[j]) dup=true;
        if(order[0]>5||order[1]>5||order[2]>5||order[3]>5||order[4]>5||order[5]>5||dup){
            for(int i=0;i<6;i++) order[i]=i;
        }
        String json = "{\"order\":[";
        for(int i=0;i<6;i++){if(i>0)json+=',';json+=String(order[i]);}
        json += "]}";
        server_->send(200, "application/json", json);
    });

    server_->on("/api/card_order", HTTP_POST, []() {
        String body = server_->arg("plain");
        int s=body.indexOf('['), e=body.indexOf(']');
        if(s>=0 && e>s){
            String arr = body.substring(s+1, e);
            for(int idx=0; idx<6; idx++){
                int c=arr.indexOf(',');
                String num = (c>=0)?arr.substring(0,c):arr;
                int v = num.toInt();
                if(v<0||v>5) v=idx;
                EEPROM.write(EEPROM_CARD_ORDER_ADDR+idx, (uint8_t)v);
                if(c<0) break;
                arr = arr.substring(c+1);
            }
            EEPROM.commit();
        }
        server_->send(200, "application/json", "{\"ok\":true}");
    });

    server_->on("/api/card_visibility", HTTP_POST, []() {
        String body = server_->arg("plain");
        int s = body.indexOf(':'), e = body.indexOf('}');
        if (s >= 0 && e > s) {
            String val = body.substring(s + 1, e);
            val.trim();
            uint8_t v = (uint8_t)val.toInt();
            EEPROM.write(EEPROM_CARD_VISIBILITY_ADDR, v);
            EEPROM.commit();
        }
        server_->send(200, "application/json", "{\"ok\":true}");
    });

    server_->on("/api/button_auto_timer", HTTP_GET, []() {
        String enabled = server_->arg("enabled");
        GPIOManager::setButtonAutoTimerEnabled(enabled == "true");
        server_->send(200, "application/json", "{\"ok\":true}");
    });

    server_->on("/api/ap_suffix", HTTP_GET, []() {
        uint8_t v = EEPROM.read(EEPROM_AP_SUFFIX_ADDR);
        server_->send(200, "application/json", "{\"suffix\":" + String(v) + "}");
    });

    server_->on("/api/ap_suffix", HTTP_POST, []() {
        String body = server_->arg("plain");
        int s=body.indexOf(':'), e=body.indexOf('}');
        if(s>=0 && e>s){
            String val = body.substring(s+1, e);
            val.trim();
            int v = val.toInt();
            if (v < 0) v = 0;
            if (v > 255) v = 255;
            EEPROM.write(EEPROM_AP_SUFFIX_ADDR, v);
            EEPROM.commit();
            WiFiManager::setAPSuffix(String(v).c_str());
        }
        server_->send(200, "application/json", "{\"ok\":true}");
    });

    server_->on("/api/hostname", HTTP_GET, []() {
        String json = "{\"hostname\":\"" + WiFiManager::getHostname() + "\"}";
        server_->send(200, "application/json", json);
    });

    server_->on("/api/hostname", HTTP_POST, []() {
        String body = server_->arg("plain");
        String hostname;
        int idx = body.indexOf("\"hostname\"");
        if (idx >= 0) {
            int colon = body.indexOf(':', idx);
            int q1 = body.indexOf('\"', colon);
            int q2 = body.indexOf('\"', q1 + 1);
            if (q1 >= 0 && q2 > q1) {
                hostname = body.substring(q1 + 1, q2);
            }
        }
        hostname.trim();
        hostname.toLowerCase();
        if (WiFiManager::setHostname(hostname.c_str())) {
            server_->send(200, "application/json", "{\"ok\":true}");
        } else {
            server_->send(400, "application/json", "{\"ok\":false,\"error\":\"invalid hostname\"}");
        }
    });

    static bool connect_pending_ = false;
    static String connect_ssid_;
    static bool connect_was_wifi_detect_ = false;
    static unsigned long connect_start_ = 0;

    server_->on("/api/connect", HTTP_POST, []() {
        String ssid = server_->arg("ssid");
        String password = server_->arg("password");

        if (ssid.length() == 0) {
            server_->send(400, "application/json", "{\"ok\":false,\"error\":\"SSID不能为空\"}");
            return;
        }

        connect_pending_ = true;
        connect_ssid_ = ssid;
        connect_was_wifi_detect_ = GPIOManager::isWiFiDetectEnabled();
        connect_start_ = millis();

        if (connect_was_wifi_detect_) {
            GPIOManager::setWiFiDetectEnabled(false);
        }

        WiFiManager::saveConfig(ssid.c_str(), password.c_str());
        server_->send(200, "application/json", "{\"ok\":true,\"status\":\"connecting\"}");
    });

    server_->on("/api/connect_status", HTTP_GET, []() {
        if (!connect_pending_) {
            server_->send(200, "application/json", "{\"status\":\"idle\"}");
            return;
        }

        if (WiFi.status() == WL_CONNECTED) {
            connect_pending_ = false;
            if (connect_was_wifi_detect_) {
                GPIOManager::setWiFiDetectEnabled(true);
            }
            server_->send(200, "application/json", "{\"ok\":true,\"status\":\"connected\",\"ssid\":\"" + connect_ssid_ + "\",\"ip\":\"" + WiFi.localIP().toString() + "\"}");
        } else if (millis() - connect_start_ > 15000) {
            connect_pending_ = false;
            if (connect_was_wifi_detect_) {
                GPIOManager::setWiFiDetectEnabled(true);
            }
            WiFi.disconnect();
            server_->send(200, "application/json", "{\"ok\":false,\"status\":\"failed\",\"error\":\"连接失败，请检查密码\"}");
        } else {
            server_->send(200, "application/json", "{\"status\":\"connecting\",\"elapsed\":" + String(millis() - connect_start_) + "}");
        }
    });

    server_->on("/api/settings", HTTP_GET, []() {
        String redLed = server_->arg("redLed");
        if (redLed == "toggle") {
            GPIOManager::setRedLedEnabled(!GPIOManager::isRedLedEnabled());
        }
        String response = "{\"redLed\":" + String(GPIOManager::isRedLedEnabled() ? "true" : "false") + "}";
        server_->send(200, "application/json", response);
    });

    server_->on("/api/ap_pass", HTTP_POST, []() {
        String pass = server_->arg("pass");
        WiFiManager::saveAPPassword(pass.c_str());
        server_->send(200, "application/json", "{\"ok\":true}");
    });

    // STA 连接后自动关闭 AP 开关
    server_->on("/api/auto_close_ap", HTTP_GET, []() {
        server_->send(200, "application/json",
            String("{\"enabled\":") + (WiFiManager::isAutoCloseAPEnabled() ? "true" : "false") + "}");
    });

    server_->on("/api/auto_close_ap", HTTP_POST, []() {
        if (!server_->hasArg("plain")) {
            server_->send(400, "application/json", "{\"ok\":false}");
            return;
        }
        String body = server_->arg("plain");
        bool enabled = (body.indexOf("\"enabled\":true") >= 0) || (body.indexOf("\"enabled\":1") >= 0);
        WiFiManager::setAutoCloseAP(enabled);
        server_->send(200, "application/json",
            String("{\"ok\":true,\"enabled\":") + (enabled ? "true" : "false") + "}");
    });

    // WiFi 发射功率限制开关
    server_->on("/api/wifi_tx_power", HTTP_GET, []() {
        server_->send(200, "application/json",
            String("{\"enabled\":") + (WiFiManager::isWiFiTxPowerLimited() ? "true" : "false") + "}");
    });

    server_->on("/api/wifi_tx_power", HTTP_POST, []() {
        if (!server_->hasArg("plain")) {
            server_->send(400, "application/json", "{\"ok\":false}");
            return;
        }
        String body = server_->arg("plain");
        bool enabled = (body.indexOf("\"enabled\":true") >= 0) || (body.indexOf("\"enabled\":1") >= 0);
        WiFiManager::setWiFiTxPowerLimited(enabled);
        server_->send(200, "application/json",
            String("{\"ok\":true,\"enabled\":") + (enabled ? "true" : "false") + "}");
    });

    // 系统日志开关
    server_->on("/api/sys_log", HTTP_GET, []() {
        server_->send(200, "application/json",
            String("{\"enabled\":") + (log_buffer_is_enabled() ? "true" : "false") + "}");
    });

    server_->on("/api/sys_log", HTTP_POST, []() {
        if (!server_->hasArg("plain")) {
            server_->send(400, "application/json", "{\"ok\":false}");
            return;
        }
        String body = server_->arg("plain");
        bool enabled = (body.indexOf("\"enabled\":true") >= 0) || (body.indexOf("\"enabled\":1") >= 0);
        log_buffer_set_enabled(enabled);
        server_->send(200, "application/json",
            String("{\"ok\":true,\"enabled\":") + (enabled ? "true" : "false") + "}");
    });

    server_->on("/api/restart", HTTP_POST, []() {
        server_->send(200, "application/json", "{\"ok\":true}");
        delay(100);
        ESP.restart();
    });

    server_->on("/api/reset", HTTP_POST, []() {
        for (int i = 0; i < 1024; i++) {
            if (i == EEPROM_SY7T609_ENABLED_ADDR) {
                EEPROM.write(i, 1); // 电量检测默认开启
            } else {
                EEPROM.write(i, 0);
            }
        }
        EEPROM.commit();
        // 重新初始化默认配置，确保各模块状态一致
        GPIOManager::reset();
        WiFiManager::reset();
        EnergyManager::reset();
        server_->send(200, "application/json", "{\"ok\":true}");
        delay(100);
        ESP.restart();
    });

    server_->on("/api/log", HTTP_GET, []() {
        String json = "[";
        int count = log_buffer_count();
        for (int i = 0; i < count; i++) {
            if (i > 0) json += ",";
            json += "\"";
            const char* line = log_buffer_get_line(i);
            for (int j = 0; line[j]; j++) {
                char c = line[j];
                if (c == '"' || c == '\\') json += '\\';
                if (c == '\n') {
                    json += "\\n";
                } else if (c == '\r') {
                    json += "\\r";
                } else if ((unsigned char)c >= 32 && (unsigned char)c <= 126) {
                    json += c;
                } else {
                    json += '?';
                }
            }
            json += "\"";
        }
        json += "]";
        server_->send(200, "application/json", json);
    });

    server_->on("/api/crash_log", HTTP_GET, []() {
        char crash_logs[CRASH_LOG_MAX_LINES][CRASH_LOG_LINE_LEN];
        int crash_count = 0;
        bool has_crash = log_buffer_load_crash_logs(crash_logs, CRASH_LOG_MAX_LINES, &crash_count);
        String json = "{";
        json += "\"reason\":\"";
        String reason = ESP.getResetReason();
        for (size_t i = 0; i < reason.length(); i++) {
            char c = reason[i];
            if (c == '"' || c == '\\') json += '\\';
            if (c >= 32 && c <= 126) json += c; else json += '?';
        }
        json += "\",\"logs\":[";
        if (has_crash) {
            for (int i = 0; i < crash_count; i++) {
                if (i > 0) json += ",";
                json += "\"";
                for (int j = 0; crash_logs[i][j]; j++) {
                    char c = crash_logs[i][j];
                    if (c == '"' || c == '\\') json += '\\';
                    if (c == '\n') {
                        json += "\\n";
                    } else if (c == '\r') {
                        json += "\\r";
                    } else if ((unsigned char)c >= 32 && (unsigned char)c <= 126) {
                        json += c;
                    } else {
                        json += '?';
                    }
                }
                json += "\"";
            }
        }
        json += "]}";
        server_->send(200, "application/json", json);
    });

    server_->on("/api/meter", HTTP_GET, []() {
        String action = server_->arg("a");
        if (action == "on") {
            SY7T609::setEnabled(true);
        } else if (action == "off") {
            SY7T609::setEnabled(false);
        }
        String response = "{\"enabled\":" + String(SY7T609::isEnabled() ? "true" : "false") + "}";
        server_->send(200, "application/json", response);
    });

    // 电表校准 API（P1）
    // a=v&v=220.0   电压自动校准
    // a=i&v=4.545   电流自动校准
    // a=reset       恢复出厂校准
    // a=save        保存当前寄存器到芯片 flash
    // a=clear       清零电能计数器
    server_->on("/api/meter_calib", HTTP_GET, []() {
        String action = server_->arg("a");
        String response;
        if (SY7T609::isFlashMode()) {
            response = "{\"ok\":false,\"error\":\"flash mode\"}";
        } else if (!SY7T609::isReady()) {
            response = "{\"ok\":false,\"error\":\"meter not ready\"}";
        } else if (action == "v") {
            float v = server_->arg("v").toFloat();
            bool ok = SY7T609::calibrateVoltage(v);
            response = "{\"ok\":" + String(ok ? "true" : "false") + ",\"action\":\"voltage\"}";
        } else if (action == "i") {
            float i = server_->arg("v").toFloat();
            bool ok = SY7T609::calibrateCurrent(i);
            response = "{\"ok\":" + String(ok ? "true" : "false") + ",\"action\":\"current\"}";
        } else if (action == "reset") {
            bool ok = SY7T609::resetCalibration();
            response = "{\"ok\":" + String(ok ? "true" : "false") + ",\"action\":\"reset\"}";
        } else if (action == "save") {
            bool ok = SY7T609::saveCalibration();
            response = "{\"ok\":" + String(ok ? "true" : "false") + ",\"action\":\"save\"}";
        } else if (action == "clear") {
            bool ok = SY7T609::clearEnergyCounter();
            response = "{\"ok\":" + String(ok ? "true" : "false") + ",\"action\":\"clear\"}";
        } else {
            response = "{\"ok\":false,\"error\":\"invalid action\"}";
        }
        server_->send(200, "application/json", response);
    });

    server_->on("/api/history", HTTP_GET, []() {
        String json = "{\"data\":[";
        for (int i = 0; i < 7; i++) {
            if (i > 0) json += ",";
            json += String(EnergyManager::getHistory(i), 2);
        }
        json += "]}";
        server_->send(200, "application/json", json);
    });

    server_->on("/api/poweroff", HTTP_GET, []() {
        if (server_->hasArg("enabled")) {
            bool en = (server_->arg("enabled") == "true");
            GPIOManager::setPowerOffEnabled(en);
        }
        if (server_->hasArg("threshold")) {
            float th = server_->arg("threshold").toFloat();
            GPIOManager::setPowerOffThreshold(th);
        }
        char buf[128];
        snprintf(buf, sizeof(buf), "{\"enabled\":%s,\"threshold\":%.2f}",
            GPIOManager::isPowerOffEnabled() ? "true" : "false",
            GPIOManager::getPowerOffThreshold());
        server_->send(200, "application/json", buf);
    });

    server_->on("/api/billing", HTTP_GET, []() {
        if (server_->hasArg("enabled")) {
            bool en = (server_->arg("enabled") == "true");
            GPIOManager::setBillingEnabled(en);
        }
        if (server_->hasArg("threshold")) {
            float th = server_->arg("threshold").toFloat();
            GPIOManager::setBillingThreshold(th);
        }
        if (server_->hasArg("money")) {
            float money = server_->arg("money").toFloat();
            GPIOManager::setBillingThresholdMoney(money);
        }
        if (server_->hasArg("time")) {
            int minutes = server_->arg("time").toInt();
            GPIOManager::setBillingThresholdTime((uint16_t)minutes);
        }
        if (server_->hasArg("mode")) {
            int mode = server_->arg("mode").toInt();
            if (mode >= 0 && mode <= 2) {
                GPIOManager::setBillingMode((GPIOManager::BillingMode)mode);
            }
        }
        char buf[256];
        snprintf(buf, sizeof(buf),
            "{\"enabled\":%s,\"mode\":%d,\"threshold\":%.2f,\"money\":%.2f,\"time\":%d,\"used\":%.2f,\"usedMoney\":%.2f,\"usedTime\":%u,\"stopReason\":%d,\"startEnergy\":%.2f,\"price\":%.2f}",
            GPIOManager::isBillingEnabled() ? "true" : "false",
            (int)GPIOManager::getBillingMode(),
            GPIOManager::getBillingThreshold(),
            GPIOManager::getBillingThresholdMoney(),
            GPIOManager::getBillingThresholdTime(),
            GPIOManager::getBillingUsedEnergy() / 1000.0f,
            GPIOManager::getBillingUsedMoney(),
            GPIOManager::getBillingUsedMinutes(),
            (int)GPIOManager::getBillingStopReason(),
            GPIOManager::getBillingStartEnergy() / 1000.0f,
            GPIOManager::getEnergyPrice());
        server_->send(200, "application/json", buf);
    });

    server_->on("/api/billing", HTTP_POST, []() {
        if (server_->hasArg("price")) {
            float price = server_->arg("price").toFloat();
            if (price > 0 && price < 10) {
                GPIOManager::setEnergyPrice(price);
            }
        }
        if (server_->hasArg("mode")) {
            int mode = server_->arg("mode").toInt();
            if (mode >= 0 && mode <= 2) {
                GPIOManager::setBillingMode((GPIOManager::BillingMode)mode);
            }
        }
        if (server_->hasArg("money")) {
            float money = server_->arg("money").toFloat();
            GPIOManager::setBillingThresholdMoney(money);
        }
        if (server_->hasArg("time")) {
            int minutes = server_->arg("time").toInt();
            GPIOManager::setBillingThresholdTime((uint16_t)minutes);
        }
        if (server_->hasArg("threshold")) {
            float th = server_->arg("threshold").toFloat();
            GPIOManager::setBillingThreshold(th);
        }
        server_->send(200, "application/json", "{\"ok\":true}");
    });

    server_->on("/api/billing/history", HTTP_GET, []() {
        String json = "{\"ok\":true,\"history\":[";
        uint8_t count = GPIOManager::getBillingHistoryCount();
        for (uint8_t i = 0; i < count; i++) {
            GPIOManager::BillingRecord rec = GPIOManager::getBillingHistory(i);
            char item[128];
            snprintf(item, sizeof(item),
                "{\"startTime\":%u,\"usedEnergy\":%.2f,\"cost\":%.2f}%s",
                rec.startTime, rec.usedEnergyWh, rec.costCents / 100.0f,
                (i < count - 1) ? "," : "");
            json += item;
        }
        json += "]}";
        server_->send(200, "application/json", json);
    });

    server_->on("/api/time", HTTP_GET, []() {
        if (server_->hasArg("ts")) {
            time_t ts = server_->arg("ts").toInt();
            struct timeval tv = { .tv_sec = ts, .tv_usec = 0 };
            settimeofday(&tv, NULL);
            configTime(8 * 3600, 0, "pool.ntp.org", "time.nist.gov");
        }
        server_->send(200, "text/plain", "OK");
    });

    server_->on("/api/now", HTTP_GET, []() {
        time_t now = time(nullptr);
        struct tm* timeinfo = localtime(&now);
        char buf[64];
        if (timeinfo && timeinfo->tm_year >= 100) {
            snprintf(buf, sizeof(buf), "{\"ok\":true,\"hour\":%d,\"min\":%02d,\"str\":\"%02d:%02d\"}",
                timeinfo->tm_hour, timeinfo->tm_min,
                timeinfo->tm_hour, timeinfo->tm_min);
        } else {
            snprintf(buf, sizeof(buf), "{\"ok\":false,\"str\":\"--:--\"}");
        }
        server_->send(200, "application/json", buf);
    });

    server_->on("/api/wifidetect", HTTP_GET, []() {
        if (server_->hasArg("enabled")) {
            GPIOManager::setWiFiDetectEnabled(server_->arg("enabled") == "true");
        }
        if (server_->hasArg("target")) {
            GPIOManager::setWiFiDetectTarget(server_->arg("target").c_str());
        }
        if (server_->hasArg("timeout")) {
            GPIOManager::setWiFiDetectTimeout(server_->arg("timeout").toInt());
        }
        if (server_->hasArg("linkTimer")) {
            GPIOManager::setWiFiDetectLinkTimer(server_->arg("linkTimer") == "true");
        }
        if (server_->hasArg("rssi")) {
            GPIOManager::setWiFiDetectRssiThreshold(server_->arg("rssi").toInt());
        }
        if (server_->hasArg("scanSpeed")) {
            GPIOManager::setWiFiDetectScanSpeed((uint8_t)server_->arg("scanSpeed").toInt());
        }
        if (server_->hasArg("onlyMaster")) {
            GPIOManager::setWiFiDetectRelayOnlyMaster(server_->arg("onlyMaster") == "true");
        }
        char buf[256];
        snprintf(buf, sizeof(buf), "{\"enabled\":%s,\"target\":\"%s\",\"timeout\":%d,\"linkTimer\":%s,\"rssi\":%d,\"scanSpeed\":%d,\"mac\":\"%s\",\"present\":%s,\"powerCheck\":%s,\"onlyMaster\":%s}",
            GPIOManager::isWiFiDetectEnabled() ? "true" : "false",
            GPIOManager::getWiFiDetectTarget().c_str(),
            GPIOManager::getWiFiDetectTimeout(),
            GPIOManager::isWiFiDetectLinkTimer() ? "true" : "false",
            GPIOManager::getWiFiDetectRssiThreshold(),
            GPIOManager::getWiFiDetectScanSpeed(),
            GPIOManager::getWiFiDetectMac().c_str(),
            GPIOManager::isWiFiDetectPresent() ? "true" : "false",
            GPIOManager::isWiFiDetectPowerChecking() ? "true" : "false",
            GPIOManager::getWiFiDetectRelayOnlyMaster() ? "true" : "false");
        server_->send(200, "application/json", buf);
    });

    server_->on("/api/button_detect", HTTP_GET, []() {
        if (server_->hasArg("enabled")) {
            GPIOManager::setButtonDetectEnabled(server_->arg("enabled") == "true");
        }
        char buf[64];
        snprintf(buf, sizeof(buf), "{\"enabled\":%s}",
            GPIOManager::isButtonDetectEnabled() ? "true" : "false");
        server_->send(200, "application/json", buf);
    });

    // 扫描 WiFi 网络（改为异步扫描并在等待期间喂狗，避免同步阻塞触发 Hardware Watchdog）
    server_->on("/api/scan", HTTP_GET, []() {
        int8_t scanStatus = WiFi.scanComplete();
        if (scanStatus == WIFI_SCAN_RUNNING) {
            // 已有扫描在运行，等待其完成
            unsigned long scanStart = millis();
            while (WiFi.scanComplete() == WIFI_SCAN_RUNNING) {
                yield();
                ESP.wdtFeed();
                if (millis() - scanStart > 10000) {
                    server_->send(200, "application/json", "[]");
                    return;
                }
            }
            scanStatus = WiFi.scanComplete();
        } else if (scanStatus < 0) {
            // 启动新异步扫描
            WiFi.scanNetworks(true, true);
            unsigned long scanStart = millis();
            while (WiFi.scanComplete() == WIFI_SCAN_RUNNING) {
                yield();
                ESP.wdtFeed();
                if (millis() - scanStart > 10000) {
                    WiFi.scanDelete();
                    server_->send(200, "application/json", "[]");
                    return;
                }
            }
            scanStatus = WiFi.scanComplete();
        }

        if (scanStatus < 0) {
            WiFi.scanDelete();
            server_->send(200, "application/json", "[]");
            return;
        }

        int n = scanStatus;
        struct WiFiAP { String ssid; int rssi; bool enc; };
        WiFiAP aps[32];
        int apCount = 0;
        for (int i = 0; i < n && apCount < 32; i++) {
            String ssid = WiFi.SSID(i);
            int rssi = WiFi.RSSI(i);
            bool enc = (WiFi.encryptionType(i) != ENC_TYPE_NONE);
            bool found = false;
            for (int j = 0; j < apCount; j++) {
                if (aps[j].ssid == ssid) {
                    found = true;
                    if (rssi > aps[j].rssi) {
                        aps[j].rssi = rssi;
                        aps[j].enc = enc;
                    }
                    break;
                }
            }
            if (!found) {
                aps[apCount++] = {ssid, rssi, enc};
            }
        }
        String json = "[";
        for (int i = 0; i < apCount; i++) {
            if (i > 0) json += ",";
            json += "{\"ssid\":\"" + aps[i].ssid + "\",\"rssi\":" + aps[i].rssi + ",\"enc\":" + (aps[i].enc ? "true" : "false") + "}";
        }
        json += "]";
        WiFi.scanDelete();
        server_->send(200, "application/json", json);
    });

    server_->on("/api/reset_energy", HTTP_POST, []() {
        EnergyManager::reset();
        // 计费会话和历史记录同时清零
        GPIOManager::setBillingStartEnergy(0);
        EEPROM.write(EEPROM_BILLING_HISTORY_COUNT_ADDR, 0);
        for (int i = 0; i < EEPROM_BILLING_HISTORY_MAX * EEPROM_BILLING_RECORD_SIZE; i++) {
            EEPROM.write(EEPROM_BILLING_HISTORY_ADDR + i, 0);
        }
        EEPROM.commit();
        server_->send(200, "application/json", "{\"ok\":true}");
    });

    server_->onNotFound([]() {
        // Captive Portal：AP 模式下所有未匹配请求重定向到配置页。
        // 使用 AP IP 而非 .local 主机名，避免 iOS 某些版本因 mDNS 解析未完成而无法弹出门户。
        String host = WiFiManager::getMDNSHostname();
        if (host.length() == 0) host = "power";
        IPAddress apIP = WiFi.softAPIP();
        String portalUrl;
        if (apIP[0] != 0) {
            portalUrl = String("http://") + apIP.toString() + "/";
        } else {
            portalUrl = String("http://") + host + ".local/";
        }
        server_->sendHeader("Location", portalUrl, true);
        server_->send(302, "text/plain", "");
    });

    server_->begin();
    DBG_PRINTF("[Web] Server started on port %d\n", WEB_SERVER_PORT);
    DBG_PRINTF("[OTA] Update endpoint: /update (user:admin pass:admin)\n");
}

void WebConfigServer::handle() {
    // 每次循环处理多个 DNS/Web 请求，降低高并发或慢客户端场景下的超时概率
    if (dnsServer_) {
        for (int i = 0; i < 4; i++) {
            dnsServer_->processNextRequest();
        }
    }
    WiFiManager::updateMDNS();  // Web 请求前让 mDNS 有机会响应查询
    // P0: 标记 Web 请求处理中，主循环检测到此标志时跳过 SY7T609 读取等阻塞操作
    // 50ms 超时保护（在 isWebRequestActive 中实现）防止死锁
    markWebRequestStart();
    for (int i = 0; i < 2; i++) {
        server_->handleClient();
    }
    markWebRequestEnd();
    WiFiManager::updateMDNS();  // Web 请求后再次响应，避免长请求期间 mDNS 查询超时
}

void WebConfigServer::setSaveCallback(void (*callback)(const char*, const char*)) {
    save_callback_ = callback;
}