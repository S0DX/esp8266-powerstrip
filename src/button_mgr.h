#ifndef BUTTON_MGR_H
#define BUTTON_MGR_H

#include <Arduino.h>

class ButtonHandler {
public:
    static void init();
    static void handle();

    typedef enum {
        CLICK_PRESS,
        CLICK_RELEASE
    } ClickType;

    static void setCallback(void (*callback)(ClickType));

private:
    static unsigned long last_release_time_;
    static unsigned long debounce_start_time_;
    static void (*callback_)(ClickType);
    static int button_state_;
};

#endif