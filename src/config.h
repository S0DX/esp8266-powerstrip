#ifndef CONFIG_H
#define CONFIG_H

#define VERSION "5.0"

// GPIO 分配（根据 cmpower 原理图）
#define BUTTON_PIN              4       // 按钮
#define RELAY_MASTER_PIN        0       // 总控继电器控制引脚（兼 flash boot 检测）
#define RELAY_SLAVE_PIN         12      // 分控继电器控制引脚
#define RELAY_ENABLE_PIN        15      // 继电器使能引脚（总控/分控共享）
#define LED_BLUE_PIN            16      // 蓝色LED
#define LED_RED_PIN             14      // 红色LED
#define LED_WHITE_PIN           5       // 分控继电器指示灯

#define RELAY_ENABLE_PULSE_MS   10      // 使能脉冲宽度（毫秒）

#define BUTTON_DEBOUNCE_MS      50
#define BUTTON_SINGLE_CLICK_MS  300
#define BUTTON_DOUBLE_WINDOW_MS 200  // 主继电器打开/关闭后等待第二次按下的窗口（ms）
#define BUTTON_LONG_PRESS_MS    20000

#define UART_TX_PIN             1       // SY7T609 通信
#define UART_RX_PIN             3
#define SY7T609_BAUD_RATE       9600

#define WEB_SERVER_PORT         80
#define OTA_PORT                8266

#define ENERGY_SAVE_INTERVAL_MS 60000

#define EEPROM_AP_SUFFIX_ADDR   257
#define EEPROM_CARD_VISIBILITY_ADDR 510
#define EEPROM_CARD_ORDER_ADDR      504  // 6 bytes (504-509): 主页卡片排序，紧邻 CYCLE_PERIODS 之后
#define EEPROM_BUTTON_AUTO_TIMER_ADDR 511
#define EEPROM_BUTTON_DETECT_ADDR 254

// 倒计时定时器 EEPROM（原定义于 gpio_mgr.cpp，统一迁移到此处）
#define EEPROM_TIMER_ENABLED_ADDR  201  // 1 byte: 0=禁用, 1=启用
#define EEPROM_TIMER_DURATION_ADDR 202  // 1 byte: 小时数 1-24（旧格式，保留用于迁移）
#define EEPROM_TIMER_VERSION_ADDR  195  // 1 byte: 0x54 表示已迁移到分钟格式
#define EEPROM_TIMER_VERSION_MAGIC 0x54
#define EEPROM_TIMER_DURATION_MIN_ADDR 196  // 2 bytes (196-197): uint16_t 总分钟数 1-1440

// EEPROM 布局版本（旧固件为 0xFF/0x01，当前 0x02 把冲突区域迁到 512+）
#define EEPROM_LAYOUT_VERSION_ADDR  143
#define EEPROM_LAYOUT_VERSION       0x02

// 自定义局域网域名存储（128-139，共 12 字节）
// 0-127 为 WiFi SSID/密码，140-198 空闲，无地址冲突
#define EEPROM_HOSTNAME_MAGIC_ADDR 128
#define EEPROM_HOSTNAME_LEN_ADDR   129
#define EEPROM_HOSTNAME_ADDR       130
#define EEPROM_HOSTNAME_MAX_LEN    10
#define EEPROM_HOSTNAME_MAGIC      0x48  // 'H'

// STA 连接成功后自动关闭 AP 开关（默认不启用）
#define EEPROM_AUTO_CLOSE_AP_ADDR  140
// 系统日志开关（默认启用）
#define EEPROM_SYS_LOG_ENABLED_ADDR 141
// WiFi 发射功率限制开关（默认不限制）
#define EEPROM_WIFI_TX_POWER_LIMIT_ADDR 142

// 人来上电（WiFi 检测）EEPROM 存储区域（218-295）
#define EEPROM_WIFIDETECT_ENABLED     218  // 1 byte
#define EEPROM_WIFIDETECT_TARGET      219  // 33 bytes (219-251)
#define EEPROM_WIFIDETECT_TIMEOUT     252  // 2 bytes (252-253)
#define EEPROM_WIFIDETECT_LINK_TIMER  255  // 1 byte
#define EEPROM_WIFIDETECT_ONLY_MASTER 256  // 1 byte
#define EEPROM_WIFIDETECT_RSSI        288  // 1 byte
#define EEPROM_WIFIDETECT_SCAN_SPEED  289  // 1 byte
#define EEPROM_WIFIDETECT_PRICE       290  // 2 bytes (290-291)
#define EEPROM_WIFIDETECT_HOURLY      396  // 96 bytes (396-491)
#define EEPROM_WIFIDETECT_MAGIC       286  // 目标数据有效标记（已从 292 迁移，避免与计费 startEnergy 冲突）
#define EEPROM_WIFIDETECT_MAGIC_ADDR_OLD 292  // 旧地址，仅用于迁移读取（迁移后清除，现址=292 已被 BILLING_START_ENERGY 占用）
#define EEPROM_WIFIDETECT_MAGIC_VAL   0x57 // 'W'

// SY7T609 电量检测 EEPROM
#define EEPROM_SY7T609_ENABLED_ADDR       210  // 1 byte: 0=禁用, 1=启用
#define EEPROM_SY7T609_FLASH_MODE_ADDR    211  // 1 byte: 0xAA=flash 模式

// 计费供电 EEPROM 存储区域（214-217, 290-387）
#define EEPROM_BILLING_ENABLED_ADDR        214  // 1 byte
#define EEPROM_BILLING_THRESHOLD_KWH_ADDR  215  // 2 bytes (215-216), uint16_t kWh*100
#define EEPROM_BILLING_CONFIG_MAGIC_ADDR   217  // 1 byte
#define EEPROM_BILLING_CONFIG_MAGIC        0xB1
#define EEPROM_BILLING_START_ENERGY_ADDR   292  // 4 bytes (292-295), float (Wh)
#define EEPROM_BILLING_MODE_ADDR           296  // 1 byte: 0=energy, 1=money, 2=time
#define EEPROM_BILLING_THRESHOLD_MONEY_ADDR 297 // 2 bytes (297-298), uint16_t 分
#define EEPROM_BILLING_THRESHOLD_TIME_ADDR  299 // 2 bytes (299-300), uint16_t 分钟
#define EEPROM_BILLING_START_TIME_ADDR      301 // 4 bytes (301-304), uint32_t unix timestamp
#define EEPROM_BILLING_STOP_REASON_ADDR     305 // 1 byte: 0=none,1=energy,2=money,3=time,4=manual
#define EEPROM_BILLING_HISTORY_COUNT_ADDR_OLD 306 // 1 byte（旧布局，仅用于迁移）
#define EEPROM_BILLING_HISTORY_ADDR_OLD       307 // 8 * 10 = 80 bytes（旧布局，仅用于迁移）
#define EEPROM_BILLING_HISTORY_COUNT_ADDR     725 // 1 byte（移至 MQTT 配置之后，避免与月度电量 552-567 冲突）
#define EEPROM_BILLING_HISTORY_ADDR           568 // 8 * 10 = 80 bytes（新布局 568-647）
#define EEPROM_BILLING_HISTORY_MAX          8
#define EEPROM_BILLING_RECORD_SIZE          10  // start_time(4) + used_energy_wh(4) + cost_cents(2)

// 电量/历史数据新版地址（512+，避开与 SSID/域名/计费/MQTT 的旧重叠）
#define EEPROM_ENERGY_TOTAL_ADDR            512 // 8 bytes: magic 4 + float 4
#define EEPROM_ENERGY_TOTAL_ADDR_OLD        128 // 旧布局，仅用于迁移
#define EEPROM_ENERGY_HISTORY_ADDR          520 // 32 bytes: magic 4 + 7*float
#define EEPROM_ENERGY_HISTORY_ADDR_OLD      32  // 旧布局，仅用于迁移
#define EEPROM_ENERGY_MONTHLY_ADDR          552 // 16 bytes: magic 4 + 3*float
#define EEPROM_ENERGY_MONTHLY_ADDR_OLD      300 // 旧布局，仅用于迁移

// MQTT 配置新版地址（648-724）
#define EEPROM_MQTT_CONFIG_ADDR             648 // 77 bytes
#define EEPROM_MQTT_MAGIC_ADDR_OLD          316 // 旧布局，仅用于迁移

// 调试输出宏：SY7T609 启用时输出到 Serial1（GPIO2），否则输出到 Serial（GPIO1）
// 注意：SY7T609 类使用前需包含 #include "sy7t609.h"
#include "log_buffer.h"
extern bool g_meter_enabled;
#define DBG_PRINTF(fmt, ...) do { \
    if (g_meter_enabled) { \
        Serial1.printf(fmt, ##__VA_ARGS__); \
    } else { \
        Serial.printf(fmt, ##__VA_ARGS__); \
    } \
    log_buffer_printf(fmt, ##__VA_ARGS__); \
} while(0)

#endif