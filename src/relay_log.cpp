#include "relay_log.h"
#include "config.h"
#include <EEPROM.h>
#include <time.h>
#include "time_mgr.h"

uint8_t RelayLog::head_ = 0;
uint8_t RelayLog::count_ = 0;

void RelayLog::init() {
    uint8_t h = EEPROM.read(EEPROM_RELAY_LOG_META_ADDR);
    uint8_t c = EEPROM.read(EEPROM_RELAY_LOG_META_ADDR + 1);
    if (h >= MAX_ENTRIES) h = 0;
    if (c > MAX_ENTRIES) c = MAX_ENTRIES;
    head_ = h;
    count_ = c;
}

bool RelayLog::add(uint8_t code, bool master, bool slave) {
    int addr = EEPROM_RELAY_LOG_DATA_ADDR + head_ * 6;
    uint32_t ts = TimeManager::isValid() ? (uint32_t)time(nullptr) : 0;
    EEPROM.write(addr,     (uint8_t)(ts & 0xFF));
    EEPROM.write(addr + 1, (uint8_t)((ts >> 8) & 0xFF));
    EEPROM.write(addr + 2, (uint8_t)((ts >> 16) & 0xFF));
    EEPROM.write(addr + 3, (uint8_t)((ts >> 24) & 0xFF));
    EEPROM.write(addr + 4, code);
    EEPROM.write(addr + 5, (uint8_t)((master ? 0x01 : 0x00) | (slave ? 0x02 : 0x00)));

    head_ = (uint8_t)((head_ + 1) % MAX_ENTRIES);
    if (count_ < MAX_ENTRIES) count_++;
    EEPROM.write(EEPROM_RELAY_LOG_META_ADDR, head_);
    EEPROM.write(EEPROM_RELAY_LOG_META_ADDR + 1, count_);
    EEPROM.commit();
    return true;
}

uint8_t RelayLog::count() { return count_; }

static const char* codeName(uint8_t code) {
    switch (code) {
        case RelayLog::EV_CHANGE:      return "变化";
        case RelayLog::EV_CYCLE_ON:    return "循环开";
        case RelayLog::EV_CYCLE_OFF:   return "循环关";
        case RelayLog::EV_TIMER_START: return "倒计时启动";
        case RelayLog::EV_TIMER_STOP:  return "倒计时停止";
        case RelayLog::EV_LATCH_ON:    return "长通开";
        case RelayLog::EV_LATCH_OFF:   return "长通关";
        case RelayLog::EV_BILLING:     return "计费动作";
        case RelayLog::EV_BOOT:        return "上电启动";
        default:                       return "?";
    }
}

int RelayLog::dump(char* out, size_t outSize) {
    if (!out || outSize == 0) return 0;
    int n = 0;
    // 从最新往最旧遍历
    for (int i = 1; i <= (int)count_ && n < (int)outSize - 1; i++) {
        uint8_t idx = (uint8_t)((head_ + MAX_ENTRIES - i) % MAX_ENTRIES);
        int addr = EEPROM_RELAY_LOG_DATA_ADDR + idx * 6;
        uint32_t ts = (uint32_t)EEPROM.read(addr)
                    | ((uint32_t)EEPROM.read(addr + 1) << 8)
                    | ((uint32_t)EEPROM.read(addr + 2) << 16)
                    | ((uint32_t)EEPROM.read(addr + 3) << 24);
        uint8_t code = EEPROM.read(addr + 4);
        uint8_t st = EEPROM.read(addr + 5);
        char tbuf[24];
        if (ts == 0) {
            snprintf(tbuf, sizeof(tbuf), "时间未sync");
        } else {
            time_t t = (time_t)ts;
            struct tm* ti = localtime(&t);
            if (ti) snprintf(tbuf, sizeof(tbuf), "%02d-%02d %02d:%02d", ti->tm_mon + 1, ti->tm_mday, ti->tm_hour, ti->tm_min);
            else snprintf(tbuf, sizeof(tbuf), "%lu", (unsigned long)ts);
        }
        int w = snprintf(out + n, outSize - n, "%s %s 主%s 副%s\n",
                         tbuf, codeName(code),
                         (st & 0x01) ? "开" : "关", (st & 0x02) ? "开" : "关");
        if (w < 0) break;
        n += w;
    }
    if (n == 0) {
        n = snprintf(out, outSize, "--");
    }
    return n;
}
