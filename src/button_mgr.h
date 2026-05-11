#ifndef BUTTON_MGR_H
#define BUTTON_MGR_H

#include <Arduino.h>

class ButtonHandler {
public:
    static void init();
    static void handle();

    typedef enum {
        CLICK_SINGLE,
        CLICK_DOUBLE,
        CLICK_LONG
    } ClickType;

    static void setCallback(void (*callback)(ClickType));

private:
    static unsigned long last_press_time_;
    static unsigned long last_release_time_;
    static int click_count_;
    static bool long_press_triggered_;
    static bool short_press_pending_;
    static bool long_press_in_progress_;
    static void (*callback_)(ClickType);
    static int button_state_;
};

#endif