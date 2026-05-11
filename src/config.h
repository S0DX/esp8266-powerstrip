#ifndef CONFIG_H
#define CONFIG_H

#define VERSION "4.9"

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
#define BUTTON_DOUBLE_CLICK_MS  300
#define BUTTON_LONG_PRESS_MS    20000

#define UART_TX_PIN             1       // SY7T609 通信
#define UART_RX_PIN             3
#define SY7T609_BAUD_RATE       9600

#define WEB_SERVER_PORT         80
#define OTA_PORT                8266

#define ENERGY_SAVE_INTERVAL_MS 30000

#define MAX_WIFI_CONFIG         3

#define EEPROM_AP_SUFFIX_ADDR   257
#define EEPROM_CARD_VISIBILITY_ADDR 510
#define EEPROM_BUTTON_AUTO_TIMER_ADDR 511
#define EEPROM_BUTTON_DETECT_ADDR 254
#define EEPROM_MDNS_HOSTNAME_ADDR 296

// UDP 设备发现广播配置
#define DISCOVERY_UDP_PORT      4210
#define DISCOVERY_INTERVAL_MS   3000  // 广播间隔 3 秒
#define DISCOVERY_BROADCAST_IP  "255.255.255.255"

// 调试输出宏：SY7T609 启用时输出到 Serial1（GPIO2），否则输出到 Serial（GPIO1）
// 注意：SY7T609 类使用前需包含 #include "sy7t609.h"
extern bool g_meter_enabled;
#define DBG_PRINTF(fmt, ...) do { \
    if (g_meter_enabled) { \
        Serial1.printf(fmt, ##__VA_ARGS__); \
    } else { \
        Serial.printf(fmt, ##__VA_ARGS__); \
    } \
} while(0)

#endif