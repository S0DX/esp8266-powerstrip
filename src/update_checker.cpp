#include "update_checker.h"
#include "config.h"
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266httpUpdate.h>
#include <WiFiClientSecure.h>

unsigned long UpdateChecker::last_check_ms_ = 0;
bool UpdateChecker::first_check_done_ = false;
bool UpdateChecker::available_ = false;
String UpdateChecker::latest_ver_ = "";
String UpdateChecker::bin_url_ = "";
String UpdateChecker::last_error_ = "";

void UpdateChecker::init() {
    first_check_done_ = false;
    available_ = false;
    latest_ver_ = "";
    bin_url_ = "";
    last_error_ = "";
}

bool UpdateChecker::available() {
    return available_;
}

String UpdateChecker::latestVersion() {
    return latest_ver_;
}

String UpdateChecker::lastError() {
    return last_error_;
}

// 从 JSON 体中提取 "key":"value" 字段（清单字段均为字符串，够用且免 JSON 库）
bool UpdateChecker::extractField(const String& body, const char* key, String& out) {
    String pat = String("\"") + key + "\":\"";
    int start = body.indexOf(pat);
    if (start < 0) return false;
    start += pat.length();
    int end = body.indexOf('"', start);
    if (end < 0 || end == start) return false;
    out = body.substring(start, end);
    return true;
}

// 语义版本比较：逐段数值比较，candidate 是否严格大于 current
bool UpdateChecker::semverGreater(const String& candidate, const String& current) {
    int ca[4] = {0, 0, 0, 0}, cb[4] = {0, 0, 0, 0};
    int na = 0, nb = 0, v = 0;
    for (size_t i = 0; i <= candidate.length() && na < 4; i++) {
        char c = i < candidate.length() ? candidate[i] : '.';
        if (c >= '0' && c <= '9') v = v * 10 + (c - '0');
        else { ca[na++] = v; v = 0; }
    }
    v = 0;
    for (size_t i = 0; i <= current.length() && nb < 4; i++) {
        char c = i < current.length() ? current[i] : '.';
        if (c >= '0' && c <= '9') v = v * 10 + (c - '0');
        else { cb[nb++] = v; v = 0; }
    }
    for (int i = 0; i < 4; i++) {
        if (ca[i] != cb[i]) return ca[i] > cb[i];
    }
    return false;
}

bool UpdateChecker::checkInternal(String& outVer, String& outBin) {
    if (WiFi.status() != WL_CONNECTED) {
        last_error_ = "offline";
        return false;
    }
    const char* urls[3] = {UPDATE_MANIFEST_URL, UPDATE_MANIFEST_URL2, UPDATE_MANIFEST_URL3};
    String errs = "";
    for (int u = 0; u < 3; u++) {
        WiFiClientSecure sec;
        sec.setInsecure();
        sec.setTimeout(6000);
        HTTPClient http;
        if (!http.begin(sec, urls[u])) {
            errs += urls[u];
            errs += ":begin;";
            continue;
        }
        http.setTimeout(6000);
        int code = http.GET();
        if (code == 200) {
            String body = http.getString();
            http.end();
            if (body.length() == 0 || body.length() > 2048) {
                errs += "bad manifest;";
                continue;
            }
            if (!extractField(body, "v", outVer)) {
                errs += "no version field;";
                continue;
            }
            extractField(body, "bin", outBin);  // bin 缺失时仅提醒不可在线升级
            last_error_ = "";
            return true;
        }
        errs += urls[u];
        errs += ":http " + String(code) + ";";
        http.end();
    }
    // 全部源失败：附带 heap 与首个错误，便于区分 DNS 污染 / TLS 内存不足
    last_error_ = "all sources failed (" + String(ESP.getFreeHeap()) + "B heap) " + errs;
    return false;
}

bool UpdateChecker::checkNow() {
    String ver, bin;
    if (!checkInternal(ver, bin)) {
        available_ = false;
        return false;
    }
    latest_ver_ = ver;
    bin_url_ = bin;
    available_ = semverGreater(ver, VERSION);
    return true;
}

bool UpdateChecker::doUpgrade() {
    if (bin_url_.length() == 0) {
        last_error_ = "no bin url";
        return false;
    }
    WiFiClientSecure sec;
    sec.setInsecure();
    sec.setTimeout(15000);
    ESPhttpUpdate.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    ESPhttpUpdate.rebootOnUpdate(true);
    Serial.printf("[Update] upgrading to %s from %s\n", latest_ver_.c_str(), bin_url_.c_str());
    t_httpUpdate_return ret = ESPhttpUpdate.update(sec, bin_url_, VERSION);
    // 成功时设备已重启，不会走到这里
    switch (ret) {
        case HTTP_UPDATE_FAILED:
            last_error_ = "update failed: " + String(ESPhttpUpdate.getLastErrorString().c_str());
            break;
        case HTTP_UPDATE_NO_UPDATES:
            last_error_ = "no updates";
            break;
        default:
            last_error_ = "unknown result";
            break;
    }
    Serial.printf("[Update] failed: %s\n", last_error_.c_str());
    return false;
}

void UpdateChecker::handle() {
    if (!first_check_done_) {
        if (millis() >= UPDATE_FIRST_CHECK_MS) {
            first_check_done_ = true;
            last_check_ms_ = millis();
            if (checkNow()) {
                Serial.printf("[Update] checked: latest=%s current=%s available=%d\n",
                    latest_ver_.c_str(), VERSION, available_ ? 1 : 0);
            }
        }
        return;
    }
    if (millis() - last_check_ms_ >= UPDATE_CHECK_INTERVAL_MS) {
        last_check_ms_ = millis();
        checkNow();
    }
}
