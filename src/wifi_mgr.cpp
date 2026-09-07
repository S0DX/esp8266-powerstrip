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
bool WiFiManager::mdns_started_ = false;
bool WiFiManager::auto_close_ap_ = false;
bool WiFiManager::ap_closed_by_auto_ = false;
bool WiFiManager::tx_power_limited_ = false;
unsigned long WiFiManager::sta_connected_time_ = 0;
unsigned long WiFiManager::last_sleep_update_ = 0;
WiFiSleepType WiFiManager::current_sleep_mode_ = WIFI_NONE_SLEEP;
// P1: 动态功率提升状态
float WiFiManager::current_tx_power_ = 14.0f;          // 默认 14dBm
unsigned long WiFiManager::power_boost_until_ = 0;      // 0=未提升

static char saved_ssid_[33] = {0};
static char saved_password_[65] = {0};
static String ap_ssid_ = "PowerStrip";

#define EEPROM_SSID_ADDR     0
#define EEPROM_PASS_ADDR     64
#define EEPROM_MAGIC         0xAB
#define EEPROM_MAGIC_ADDR    258
#define EEPROM_AP_PASS_ADDR  259
#define EEPROM_AP_MAGIC      0xCA
#define EEPROM_SIZE          1024
#define WIFI_CONNECT_TIMEOUT 8000
#define RECONNECT_INTERVAL   5000
#define MAX_RECONNECT_ATTEMPTS 3

void WiFiManager::init() {
    buildAPSSID();

    // 读取 STA 连接后自动关闭 AP 开关（默认不启用）
    auto_close_ap_ = (EEPROM.read(EEPROM_AUTO_CLOSE_AP_ADDR) == 0x01);
    ap_closed_by_auto_ = false;
    sta_connected_time_ = 0;

    // 读取 WiFi 发射功率限制开关（默认不限制）
    tx_power_limited_ = (EEPROM.read(EEPROM_WIFI_TX_POWER_LIMIT_ADDR) == 0x01);

    WiFi.mode(WIFI_AP_STA);
    // 默认启用 MODEM_SLEEP，后续根据 AP 客户端连接状态动态调整
    WiFi.setSleepMode(WIFI_MODEM_SLEEP);
    current_sleep_mode_ = WIFI_MODEM_SLEEP;
    // 关闭 SDK 自动连接与持久化，避免后台自动写 flash/重连与手动逻辑冲突导致看门狗卡死
    WiFi.setAutoConnect(false);
    WiFi.persistent(false);
    // 启动功率 14dBm：确保能连上路由器（8dBm 在隔墙场景信号太弱导致 TCP 频繁重传）
    // BOD 风险通过 flushPendingEEPROM 避让 WiFi 扫描 + EnergyManager 延迟提交来缓解
    // 连接后由 computeTargetTxPower 根据信号强度动态调整
    WiFi.setOutputPower(14.0);

    // 读取 AP 密码
    String apPass = getAPPassword();
    if (apPass.length() >= 8) {
        WiFi.softAP(ap_ssid_.c_str(), apPass.c_str());
    } else {
        WiFi.softAP(ap_ssid_.c_str(), "");  // 开放热点
    }

    // 等待 AP 就绪，缩短单次 delay 并在期间让出 CPU，避免 setup 阶段看门狗超时
    for (int i = 0; i < 6; i++) {
        yield();
        delay(100);
        ESP.wdtFeed();
    }

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

// 根据 RSSI 计算目标发射功率，加入滞回（hysteresis）避免在阈值边界抖动
// 当前功率较高时，需要信号明显改善才降级；当前功率较低时，信号明显恶化才升级
static float computeTargetTxPower(int rssi, float current_power, bool limited) {
    const int HYSTERESIS_DB = 3;
    // 未限制模式恢复 14dBm；限制模式最弱档 12dBm（10dBm 在隔墙场景信号弱
    // 导致 TCP 重传和页面加载失败，上调折中省电与稳定）
    float max_weak = limited ? 12.0f : 14.0f;

    if (current_power <= 4.0f) {
        // 当前 4dBm：RSSI 变差到 -43 以下才升功率
        if (rssi <= -40 - HYSTERESIS_DB) {
            if (rssi <= -60 + HYSTERESIS_DB) {
                return max_weak;
            }
            return 8.0f;
        }
        return 4.0f;
    } else if (current_power <= 8.0f) {
        // 当前 8dBm：RSSI 变好到 -37 以上才降级，变差到 -63 以下才继续升级
        if (rssi > -40 + HYSTERESIS_DB) {
            return 4.0f;
        } else if (rssi <= -60 - HYSTERESIS_DB) {
            return max_weak;
        }
        return 8.0f;
    } else {
        // 当前 14/10dBm：RSSI 变好到 -57 以上才降级
        if (rssi > -60 + HYSTERESIS_DB) {
            if (rssi > -40 - HYSTERESIS_DB) {
                return 4.0f;
            }
            return 8.0f;
        }
        return max_weak;
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

    // 动态调整 WiFi sleep 模式，平衡响应速度与功耗
    updateSleepMode();

    // mDNS 查询响应需及时处理，独立于 STA 状态调用（Web 请求间隙也由外部 updateMDNS 兜底）
    if (mdns_started_) {
        MDNS.update();
    }

    if (state_ == STATE_CONNECTED) {
        int rssi = WiFi.RSSI();
        // P1: boost 期间不调整功率（保持高功率），由 handleTxPowerTimeout 负责恢复
        if (power_boost_until_ == 0) {
            float target_power = computeTargetTxPower(rssi, current_tx_power_, tx_power_limited_);
            // 只在目标功率变化时才调用，避免每循环都重配射频
            if (target_power != current_tx_power_) {
                WiFi.setOutputPower(target_power);
                current_tx_power_ = target_power;
                Serial.printf("[WiFi] TX power adjusted to %.1fdBm (RSSI %d)\n", target_power, rssi);
            }
        }

        // STA 连接成功后自动关闭 AP（如果开启该选项，预留 5 分钟窗口）
        if (auto_close_ap_ && !ap_closed_by_auto_) {
            if (sta_connected_time_ == 0) {
                sta_connected_time_ = millis();
                Serial.println("[WiFi] Auto-close AP timer started (5 min window)");
            } else if (millis() - sta_connected_time_ >= 300000) {
                stopAP();
            }
        }
    }

    switch (state_) {
        case STATE_CONNECTING:
            if (WiFi.status() == WL_CONNECTED) {
                state_ = STATE_CONNECTED;
                is_connecting_ = false;
                connection_failures_ = 0;
                sta_connected_time_ = millis();
                Serial.printf("[WiFi] Connected! IP: %s\n", WiFi.localIP().toString().c_str());
                GPIOManager::setRedMode(GPIOManager::LED_OFF);
                configTime(8 * 3600, 0, "pool.ntp.org", "time.nist.gov"); // 同步北京时间
                startMDNS();
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
                if (mdns_started_) {
                    MDNS.close();
                    mdns_started_ = false;
                }
            }
            break;

        case STATE_DISCONNECTED:
            if (WiFi.status() == WL_CONNECTED) {
                state_ = STATE_CONNECTED;
                connection_failures_ = 0;
                sta_connected_time_ = millis();
                GPIOManager::setRedMode(GPIOManager::LED_OFF);
                Serial.printf("[WiFi] Reconnected! IP: %s\n", WiFi.localIP().toString().c_str());
                startMDNS();
            } else if (ap_closed_by_auto_) {
                // AP 之前被自动关闭，现在 STA 断开，恢复 AP 以便重新配网
                restartAP();
            } else if (millis() - last_reconnect_attempt_ > RECONNECT_INTERVAL) {
                if (connection_failures_ < MAX_RECONNECT_ATTEMPTS) {
                    Serial.printf("[WiFi] Attempting reconnect %d/%d...\n", connection_failures_ + 1, MAX_RECONNECT_ATTEMPTS);
                    state_ = STATE_CONNECTING;
                    connection_start_time_ = millis();
                    is_connecting_ = true;
                    if (mdns_started_) {
                        MDNS.close();
                        mdns_started_ = false;
                    }
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
                GPIOManager::setRedMode(GPIOManager::LED_OFF);
                startMDNS();
            }
            break;

        default:
            break;
    }
}

bool WiFiManager::isConnected() {
    return state_ == STATE_CONNECTED;
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

String WiFiManager::getMDNSHostname() {
    return getHostname();
}

String WiFiManager::getHostname() {
    if (EEPROM.read(EEPROM_HOSTNAME_MAGIC_ADDR) != EEPROM_HOSTNAME_MAGIC) {
        return "power";
    }
    uint8_t len = EEPROM.read(EEPROM_HOSTNAME_LEN_ADDR);
    if (len == 0 || len > EEPROM_HOSTNAME_MAX_LEN) {
        return "power";
    }
    char buf[EEPROM_HOSTNAME_MAX_LEN + 1] = {0};
    for (uint8_t i = 0; i < len; i++) {
        char c = (char)EEPROM.read(EEPROM_HOSTNAME_ADDR + i);
        // 只允许小写字母、数字和连字符
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-') {
            buf[i] = c;
        } else {
            return "power";
        }
    }
    buf[len] = '\0';
    return String(buf);
}

bool WiFiManager::setHostname(const char* hostname) {
    if (hostname == nullptr) {
        return false;
    }
    size_t len = strlen(hostname);
    if (len < 1 || len > EEPROM_HOSTNAME_MAX_LEN) {
        return false;
    }
    // 首字符必须是字母或数字
    char first = hostname[0];
    if (!((first >= 'a' && first <= 'z') || (first >= '0' && first <= '9'))) {
        return false;
    }
    // 尾字符不能是连字符
    char last = hostname[len - 1];
    if (last == '-') {
        return false;
    }
    // 仅允许小写字母、数字、连字符
    for (size_t i = 0; i < len; i++) {
        char c = hostname[i];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-')) {
            return false;
        }
    }

    EEPROM.write(EEPROM_HOSTNAME_MAGIC_ADDR, EEPROM_HOSTNAME_MAGIC);
    EEPROM.write(EEPROM_HOSTNAME_LEN_ADDR, (uint8_t)len);
    for (size_t i = 0; i < len; i++) {
        EEPROM.write(EEPROM_HOSTNAME_ADDR + i, hostname[i]);
    }
    EEPROM.commit();

    // 如果 mDNS 已启动，重启它以应用新域名
    if (mdns_started_) {
        startMDNS();
    }
    return true;
}

void WiFiManager::resetHostname() {
    EEPROM.write(EEPROM_HOSTNAME_MAGIC_ADDR, 0);
    EEPROM.commit();
    if (mdns_started_) {
        startMDNS();
    }
}

void WiFiManager::startMDNS() {
    if (mdns_started_) {
        MDNS.close();
        mdns_started_ = false;
    }

    String hostname = getMDNSHostname();

    // ESP8266mDNS 在 AP_STA 双模下可能绑定到 AP 接口导致 .local 无法解析。
    // 临时切到 STA-only 启动 mDNS，确保它绑定到正确的 STA IP，然后再恢复 AP_STA。
    bool had_ap = (WiFi.getMode() & WIFI_AP) != 0;
    if (had_ap) {
        WiFi.mode(WIFI_STA);
        delay(50);
        ESP.wdtFeed();
        yield();
    }

    // MDNS.begin() 在 WiFi 刚连接时可能因网络栈未就绪而失败，重试 3 次
    bool ok = false;
    for (int i = 0; i < 3 && !ok; i++) {
        ok = MDNS.begin(hostname.c_str());
        if (!ok) {
            Serial.printf("[mDNS] begin attempt %d failed, retrying...\n", i + 1);
            delay(200);
            ESP.wdtFeed();
            yield();
        }
    }
    ESP.wdtFeed();
    yield();

    if (had_ap) {
        WiFi.mode(WIFI_AP_STA);
        delay(50);
        ESP.wdtFeed();
        yield();
        String apPass = getAPPassword();
        if (apPass.length() >= 8) {
            WiFi.softAP(ap_ssid_.c_str(), apPass.c_str());
        } else {
            WiFi.softAP(ap_ssid_.c_str(), "");
        }
        yield();
    }

    if (ok) {
        mdns_started_ = true;
        MDNS.addService("http", "tcp", WEB_SERVER_PORT);
        Serial.printf("[mDNS] Started: %s.local -> %s\n", hostname.c_str(), WiFi.localIP().toString().c_str());
    } else {
        mdns_started_ = false;
        Serial.println("[mDNS] Failed to start");
    }
}

void WiFiManager::updateMDNS() {
    if (mdns_started_) {
        MDNS.update();
    }
}

// 根据 AP 客户端连接状态和 STA 状态动态切换 WiFi sleep 模式：
// - 有 AP 客户端连接或 STA 未连接时保持 NONE_SLEEP，保证响应速度
// - STA 已连接且无 AP 客户端时启用 MODEM_SLEEP，降低功耗和发热
void WiFiManager::updateSleepMode() {
    if (millis() - last_sleep_update_ < 1000) {
        return;
    }
    last_sleep_update_ = millis();

    WiFiSleepType target = WIFI_MODEM_SLEEP;
    if (state_ != STATE_CONNECTED) {
        target = WIFI_NONE_SLEEP;
    } else if (WiFi.softAPgetStationNum() > 0) {
        target = WIFI_NONE_SLEEP;
    }

    if (target != current_sleep_mode_) {
        WiFi.setSleepMode(target);
        current_sleep_mode_ = target;
        Serial.printf("[WiFi] Sleep mode switched to %s\n",
            target == WIFI_NONE_SLEEP ? "NONE" : "MODEM");
    }
}

void WiFiManager::stopAP() {
    if ((WiFi.getMode() & WIFI_AP) == 0) return;
    WiFi.softAPdisconnect(false);
    WiFi.mode(WIFI_STA);
    ap_closed_by_auto_ = true;
    Serial.println("[WiFi] AP auto-closed, STA mode only");
}

void WiFiManager::restartAP() {
    if ((WiFi.getMode() & WIFI_AP) != 0) return;
    WiFi.mode(WIFI_AP_STA);
    String apPass = getAPPassword();
    if (apPass.length() >= 8) {
        WiFi.softAP(ap_ssid_.c_str(), apPass.c_str());
    } else {
        WiFi.softAP(ap_ssid_.c_str(), "");
    }
    ap_closed_by_auto_ = false;
    Serial.printf("[WiFi] AP reopened (SSID: %s)\n", ap_ssid_.c_str());
}

bool WiFiManager::isAutoCloseAPEnabled() {
    return auto_close_ap_;
}

void WiFiManager::setAutoCloseAP(bool enabled) {
    auto_close_ap_ = enabled;
    EEPROM.write(EEPROM_AUTO_CLOSE_AP_ADDR, enabled ? 0x01 : 0x00);
    EEPROM.commit();
    Serial.printf("[WiFi] Auto-close AP: %s\n", enabled ? "enabled" : "disabled");
    // 如果关闭开关且 AP 之前被自动关闭，立即恢复 AP
    if (!enabled && ap_closed_by_auto_) {
        restartAP();
    }
}

bool WiFiManager::isWiFiTxPowerLimited() {
    return tx_power_limited_;
}

void WiFiManager::setWiFiTxPowerLimited(bool enabled) {
    tx_power_limited_ = enabled;
    EEPROM.write(EEPROM_WIFI_TX_POWER_LIMIT_ADDR, enabled ? 0x01 : 0x00);
    EEPROM.commit();
    // 若已连接且未在 boost 期，立即按新限制重新计算目标功率
    if (state_ == STATE_CONNECTED && power_boost_until_ == 0) {
        int rssi = WiFi.RSSI();
        float target_power = computeTargetTxPower(rssi, current_tx_power_, enabled);
        if (target_power != current_tx_power_) {
            WiFi.setOutputPower(target_power);
            current_tx_power_ = target_power;
            Serial.printf("[WiFi] TX power adjusted to %.1fdBm (RSSI %d)\n", target_power, rssi);
        }
    }
    Serial.printf("[WiFi] TX power limit: %s\n", enabled ? "enabled" : "disabled");
}

// ============================================================================
// P1: Web 请求时临时提升发射功率
// 弱信号场景下，访问页面时短时提升到 16.5dBm，30 秒后自动恢复
// 期间禁用 EEPROM.commit 等高电流操作，避免 BOD 触发
// ============================================================================

void WiFiManager::boostTxPower() {
    constexpr float WEB_REQUEST_BOOST_POWER = 16.5f;
    constexpr unsigned long WEB_REQUEST_BOOST_MS = 30000;

    unsigned long now = millis();
    if (power_boost_until_ == 0) {
        // 首次提升
        WiFi.setOutputPower(WEB_REQUEST_BOOST_POWER);
        current_tx_power_ = WEB_REQUEST_BOOST_POWER;
        Serial.printf("[WiFi] TX power BOOSTED to %.1f dBm for %lu ms\n",
                      WEB_REQUEST_BOOST_POWER, WEB_REQUEST_BOOST_MS);
    } else if ((long)(now - power_boost_until_) >= 0) {
        // 上一轮 boost 已过期但还未恢复（不应发生，handleTxPowerTimeout 应已处理）
        // 使用 (long) 转换比较，正确处理 millis() 溢出
        WiFi.setOutputPower(WEB_REQUEST_BOOST_POWER);
        current_tx_power_ = WEB_REQUEST_BOOST_POWER;
    }
    power_boost_until_ = now + WEB_REQUEST_BOOST_MS;
}

void WiFiManager::handleTxPowerTimeout() {
    constexpr float WEB_REQUEST_NORMAL_POWER = 14.0f;
    // 使用 (long) 强制转换比较，正确处理 millis() 49.7 天溢出
    if (power_boost_until_ && (long)(millis() - power_boost_until_) >= 0) {
        power_boost_until_ = 0;
        // 恢复到正常功率（handle() 中会根据 RSSI 重新调整）
        WiFi.setOutputPower(WEB_REQUEST_NORMAL_POWER);
        current_tx_power_ = WEB_REQUEST_NORMAL_POWER;
        Serial.println(F("[WiFi] TX power restored to normal"));
    }
}

float WiFiManager::getCurrentTxPower() {
    return current_tx_power_;
}

bool WiFiManager::isTxPowerBoosted() {
    return power_boost_until_ != 0;
}