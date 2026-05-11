#include "wifi_mgr.h"
#include "config.h"
#include "gpio_mgr.h"
#include <EEPROM.h>

WiFiManager::WiFiState WiFiManager::state_ = WiFiManager::STATE_IDLE;
unsigned long WiFiManager::connection_start_time_ = 0;
unsigned long WiFiManager::last_reconnect_attempt_ = 0;
int WiFiManager::connection_failures_ = 0;
bool WiFiManager::is_connecting_ = false;
bool WiFiManager::sta_connect_pending_ = false;

static char saved_ssid_[33] = {0};
static char saved_password_[65] = {0};
static String ap_ssid_ = "PowerStrip";

#define EEPROM_SSID_ADDR     0
#define EEPROM_PASS_ADDR     64
#define EEPROM_MAGIC         0xAB
#define EEPROM_MAGIC_ADDR    254
#define EEPROM_AP_PASS_ADDR  230
#define EEPROM_AP_MAGIC      0xCA
#define EEPROM_SIZE          512
#define WIFI_CONNECT_TIMEOUT 8000
#define RECONNECT_INTERVAL   5000
#define MAX_RECONNECT_ATTEMPTS 3

void WiFiManager::init() {
    buildAPSSID();
    WiFi.mode(WIFI_AP_STA);
    WiFi.setSleepMode(WIFI_NONE_SLEEP);
    WiFi.setAutoConnect(true);
    WiFi.persistent(true);
    WiFi.setOutputPower(16.5);  // 初始最高功率，确保配置时信号强

    // 读取 AP 密码
    String apPass = getAPPassword();
    if (apPass.length() >= 8) {
        WiFi.softAP(ap_ssid_.c_str(), apPass.c_str());
    } else {
        WiFi.softAP(ap_ssid_.c_str(), "");  // 开放热点
    }
    
    // 多次 yield() 确保 AP 完全就绪，DNS 和 Web 服务器能正常响应
    yield();
    delay(500);
    yield();
    delay(500);
    yield();
    delay(500);

    // 初始化 LED 状态
    GPIOManager::setRedMode(GPIOManager::LED_BLINK);

    Serial.printf("[WiFi] AP started: %s (SSID: %s)%s\n",
        WiFi.softAPIP().toString().c_str(),
        ap_ssid_.c_str(),
        apPass.length() >= 8 ? " (password protected)" : " (open)");

    // 仅标记待连接，实际连接在 handle() 中进行，避免阻塞 AP 响应
    if (loadConfig()) {
        Serial.printf("[WiFi] Loaded config for SSID: %s\n", saved_ssid_);
        state_ = STATE_CONNECTING;
        connection_start_time_ = millis();
        is_connecting_ = true;
        sta_connect_pending_ = true;  // 标记待连接，在 handle() 中处理
    } else {
        Serial.println("[WiFi] No saved config, AP only mode");
        state_ = STATE_IDLE;
        GPIOManager::setRedMode(GPIOManager::LED_OFF);  // 无配置不敌错
    }
}

void WiFiManager::handle() {
    yield();  // 每次调用都让出 CPU，处理后台任务

    // 处理待连接的 STA 连接（非阻塞）
    if (sta_connect_pending_) {
        sta_connect_pending_ = false;
        WiFi.begin(saved_ssid_, saved_password_);
        Serial.printf("[WiFi] Starting STA connection to: %s\n", saved_ssid_);
        yield();  // 让出 CPU，确保 WiFi.begin() 能正常执行
    }

    if (state_ == STATE_CONNECTED) {
        int rssi = WiFi.RSSI();
        if (rssi > -40) {
            WiFi.setOutputPower(4.0f);
        } else if (rssi > -60) {
            WiFi.setOutputPower(8.0f);
        } else {
            WiFi.setOutputPower(14.0f);
        }
    }

    switch (state_) {
        case STATE_CONNECTING:
            if (WiFi.status() == WL_CONNECTED) {
                state_ = STATE_CONNECTED;
                is_connecting_ = false;
                connection_failures_ = 0;
                Serial.printf("[WiFi] Connected! IP: %s\n", WiFi.localIP().toString().c_str());
                GPIOManager::setRedMode(GPIOManager::LED_OFF);
                configTime(8 * 3600, 0, "pool.ntp.org", "time.nist.gov"); // 同步北京时间
                // 自适应功率调整会在 handle() 中自动执行
            } else if (millis() - connection_start_time_ > WIFI_CONNECT_TIMEOUT) {
                Serial.println("[WiFi] Connection timeout");
                is_connecting_ = false;
                connection_failures_++;
                if (connection_failures_ >= MAX_RECONNECT_ATTEMPTS) {
                    Serial.println("[WiFi] Max reconnect attempts reached, giving up");
                    state_ = STATE_IDLE;
                    GPIOManager::setRedMode(GPIOManager::LED_BLINK);
                } else {
                    Serial.printf("[WiFi] Will retry... attempt %d/%d\n", connection_failures_ + 1, MAX_RECONNECT_ATTEMPTS);
                    state_ = STATE_DISCONNECTED;
                    last_reconnect_attempt_ = millis();
                }
                yield();  // 超时处理后让出 CPU
            }
            break;

        case STATE_CONNECTED:
            if (WiFi.status() != WL_CONNECTED) {
                state_ = STATE_DISCONNECTED;
                last_reconnect_attempt_ = millis();
                Serial.println("[WiFi] Disconnected!");
                GPIOManager::setRedMode(GPIOManager::LED_BLINK);
            }
            break;

        case STATE_DISCONNECTED:
            if (WiFi.status() == WL_CONNECTED) {
                state_ = STATE_CONNECTED;
                connection_failures_ = 0;
                GPIOManager::setBlueMode(GPIOManager::LED_ON);
                GPIOManager::setRedMode(GPIOManager::LED_OFF);
                Serial.printf("[WiFi] Reconnected! IP: %s\n", WiFi.localIP().toString().c_str());
            } else if (millis() - last_reconnect_attempt_ > RECONNECT_INTERVAL) {
                if (connection_failures_ < MAX_RECONNECT_ATTEMPTS) {
                    Serial.printf("[WiFi] Attempting reconnect %d/%d...\n", connection_failures_ + 1, MAX_RECONNECT_ATTEMPTS);
                    state_ = STATE_CONNECTING;
                    connection_start_time_ = millis();
                    is_connecting_ = true;
                    WiFi.begin(saved_ssid_, saved_password_);  // 使用保存的配置
                    yield();  // 重新连接后让出 CPU
                } else {
                    state_ = STATE_IDLE;
                    Serial.println("[WiFi] Max attempts reached, staying in IDLE mode");
                }
            }
            break;

        case STATE_IDLE:
            if (WiFi.status() == WL_CONNECTED) {
                state_ = STATE_CONNECTED;
                connection_failures_ = 0;
                GPIOManager::setBlueMode(GPIOManager::LED_ON);
                GPIOManager::setRedMode(GPIOManager::LED_OFF);
            }
            break;

        default:
            break;
    }
}

bool WiFiManager::isConnected() {
    return state_ == STATE_CONNECTED;
}

bool WiFiManager::isConfigMode() {
    return true;
}

void WiFiManager::enterConfigMode() {
}

void WiFiManager::exitConfigMode() {
}

void WiFiManager::saveConfig(const char* ssid, const char* password) {
    size_t ssidLen = strlen(ssid);
    size_t passLen = strlen(password);
    if (ssidLen > 31) ssidLen = 31;
    if (passLen > 63) passLen = 63;

    memcpy(saved_ssid_, ssid, ssidLen);
    saved_ssid_[ssidLen] = 0;
    memcpy(saved_password_, password, passLen);
    saved_password_[passLen] = 0;

    for (size_t i = 0; i < 32; i++) {
        EEPROM.write(EEPROM_SSID_ADDR + i, (i < ssidLen) ? ssid[i] : 0);
    }
    for (size_t i = 0; i < 64; i++) {
        EEPROM.write(EEPROM_PASS_ADDR + i, (i < passLen) ? password[i] : 0);
    }
    EEPROM.write(EEPROM_MAGIC_ADDR, EEPROM_MAGIC);
    EEPROM.commit();

    Serial.printf("[WiFi] Config saved: %s\n", ssid);
    
    // 尝试立即连接
    state_ = STATE_CONNECTING;
    connection_start_time_ = millis();
    is_connecting_ = true;
    connection_failures_ = 0;
    WiFi.begin(ssid, password);
}

bool WiFiManager::loadConfig() {
    if (EEPROM.read(EEPROM_MAGIC_ADDR) != EEPROM_MAGIC) {
        return false;
    }

    for (int i = 0; i < 32; i++) {
        saved_ssid_[i] = EEPROM.read(EEPROM_SSID_ADDR + i);
    }
    saved_ssid_[32] = 0;

    for (int i = 0; i < 64; i++) {
        saved_password_[i] = EEPROM.read(EEPROM_PASS_ADDR + i);
    }
    saved_password_[64] = 0;

    if (strlen(saved_ssid_) == 0) {
        return false;
    }

    return true;
}

void WiFiManager::reset() {
    EEPROM.write(EEPROM_MAGIC_ADDR, 0);
    EEPROM.commit();
    memset(saved_ssid_, 0, sizeof(saved_ssid_));
    memset(saved_password_, 0, sizeof(saved_password_));
    WiFi.disconnect();
    Serial.println("[WiFi] Config reset");
}

void WiFiManager::saveAPPassword(const char* password) {
    if (password == nullptr || strlen(password) == 0) {
        EEPROM.write(EEPROM_AP_PASS_ADDR, 0);
        WiFi.softAPdisconnect();
        WiFi.softAP(ap_ssid_.c_str(), "");
    } else {
        EEPROM.write(EEPROM_AP_PASS_ADDR, EEPROM_AP_MAGIC);
        size_t len = strlen(password);
        if (len > 22) len = 22;
        for (size_t i = 0; i < 22; i++) {
            EEPROM.write(EEPROM_AP_PASS_ADDR + 1 + i, i < len ? password[i] : 0);
        }
        String newPass(password);
        WiFi.softAPdisconnect();
        WiFi.softAP(ap_ssid_.c_str(), newPass.c_str());
    }
    EEPROM.commit();
    Serial.println("[WiFi] AP password updated");
}

String WiFiManager::getAPPassword() {
    if (EEPROM.read(EEPROM_AP_PASS_ADDR) != EEPROM_AP_MAGIC) {
        return "";  // 无密码
    }
    char buf[23] = {0};
    int len = 0;
    for (int i = 0; i < 22; i++) {
        char c = EEPROM.read(EEPROM_AP_PASS_ADDR + 1 + i);
        if (c >= 32 && c <= 126) {
            buf[len++] = c;
        } else {
            break;
        }
    }
    buf[len] = '\0';
    return String(buf);
}

void WiFiManager::scanNetworks() {
    Serial.println("[WiFi] Scanning networks...");
    WiFi.scanNetworks(true);
    yield();  // 让出 CPU，确保扫描正确启动
}

int WiFiManager::getScanCount() {
    int count = WiFi.scanComplete();
    yield();  // 让出 CPU，处理后台任务
    return count;
}

String WiFiManager::getScanSSID(int i) {
    String ssid = WiFi.SSID(i);
    yield();  // 让出 CPU，处理后台任务
    return ssid;
}

int WiFiManager::getScanRSSI(int i) {
    int rssi = WiFi.RSSI(i);
    yield();  // 让出 CPU，处理后台任务
    return rssi;
}

bool WiFiManager::isScanEncrypted(int i) {
    bool encrypted = WiFi.encryptionType(i) != ENC_TYPE_NONE;
    yield();  // 让出 CPU，处理后台任务
    return encrypted;
}

void WiFiManager::connectTo(int index, const char* password) {
    int8_t scanStatus = WiFi.scanComplete();
    if (scanStatus <= 0 || index >= scanStatus) {
        Serial.println("[WiFi] Invalid scan index");
        return;
    }

    String ssid = WiFi.SSID(index);
    Serial.printf("[WiFi] Connecting to: %s\n", ssid.c_str());

    saveConfig(ssid.c_str(), password ? password : "");
    state_ = STATE_CONNECTING;
    connection_start_time_ = millis();
    is_connecting_ = true;
    connection_failures_ = 0;
    WiFi.begin(ssid.c_str(), password);
    yield();  // 让出 CPU，确保连接正确启动
}

void WiFiManager::disconnect() {
    WiFi.disconnect();
    state_ = STATE_IDLE;
}

int WiFiManager::getConnectionFailures() {
    return connection_failures_;
}

void WiFiManager::resetConnectionFailures() {
    connection_failures_ = 0;
}

WiFiManager::WiFiState WiFiManager::getState() {
    return state_;
}

String WiFiManager::getCurrentSSID() {
    if (WiFi.status() == WL_CONNECTED) {
        return WiFi.SSID();
    }
    return "";
}

int WiFiManager::getCurrentRSSI() {
    if (WiFi.status() == WL_CONNECTED) {
        return WiFi.RSSI();
    }
    return -999;
}

void WiFiManager::buildAPSSID() {
    uint8_t suffix = EEPROM.read(EEPROM_AP_SUFFIX_ADDR);
    if (suffix > 0 && suffix <= 254) {
        ap_ssid_ = "PowerStrip-" + String(suffix);
    } else {
        ap_ssid_ = "PowerStrip";
    }
}

String WiFiManager::getAPSuffix() {
    return String(EEPROM.read(EEPROM_AP_SUFFIX_ADDR));
}

void WiFiManager::setAPSuffix(const char* suffix) {
    int val = atoi(suffix);
    if (val < 0) val = 0;
    if (val > 255) val = 255;
    EEPROM.write(EEPROM_AP_SUFFIX_ADDR, (uint8_t)val);
    EEPROM.commit();
    buildAPSSID();
    String apPass = getAPPassword();
    WiFi.softAPdisconnect();
    if (apPass.length() >= 8) {
        WiFi.softAP(ap_ssid_.c_str(), apPass.c_str());
    } else {
        WiFi.softAP(ap_ssid_.c_str(), "");
    }
    Serial.printf("[WiFi] AP suffix set to %d, SSID: %s\n", val, ap_ssid_.c_str());
}