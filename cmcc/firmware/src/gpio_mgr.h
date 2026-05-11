#ifndef GPIO_MGR_H
#define GPIO_MGR_H

#include <Arduino.h>
#include <EEPROM.h>

class GPIOManager {
public:
    static void init();
    static void initRelayPins();
    static void initLEDPins();
    static void initButtonPin();

    static void setRelayMaster(bool on);
    static void setRelaySlave(bool on);
    static bool getRelayMaster();
    static bool getRelaySlave();

    static void setLocked(bool locked);
    static bool isLocked();

    static void triggerRelayEnable();

    static void setLEDBlue(bool on, bool blink = false);
    static void setLEDRed(bool on, bool blink = false);
    static void setLEDWhite(bool on);

    static void updateLEDs();

    enum LEDMode { LED_OFF, LED_ON, LED_BLINK };
    static void setBlueMode(LEDMode mode);
    static void setRedMode(LEDMode mode);

private:
    static bool relay_master_state_;
    static bool relay_slave_state_;
    static bool locked_;
    static LEDMode blue_mode_;
    static LEDMode red_mode_;
    static unsigned long blink_timer_;
    static bool blink_state_;
};

#endif
