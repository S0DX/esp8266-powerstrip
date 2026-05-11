#ifndef CONFIG_H
#define CONFIG_H

#define VERSION "1.0.0"

#define BUTTON_PIN              4
#define RELAY_MASTER_PIN        0
#define RELAY_SLAVE_PIN         12
#define RELAY_ENABLE_PIN        15
#define LED_BLUE_PIN            16
#define LED_RED_PIN             14
#define LED_WHITE_PIN           5

#define RELAY_ENABLE_PULSE_MS   10

#define BUTTON_DEBOUNCE_MS      50
#define BUTTON_SINGLE_CLICK_MS  300
#define BUTTON_DOUBLE_CLICK_MS  500
#define BUTTON_LONG_PRESS_MS    5000

#define UART_TX_PIN             1
#define UART_RX_PIN             3
#define SY7T609_BAUD_RATE       9600

#define WEB_SERVER_PORT         80
#define OTA_PORT                8266

#define ENERGY_SAVE_INTERVAL_MS 30000

#define MAX_WIFI_CONFIG         3

#endif
