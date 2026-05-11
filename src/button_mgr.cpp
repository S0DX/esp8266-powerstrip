#include "button_mgr.h"
#include "config.h"
#include "gpio_mgr.h"

unsigned long ButtonHandler::last_press_time_ = 0;
unsigned long ButtonHandler::last_release_time_ = 0;
int ButtonHandler::click_count_ = 0;
bool ButtonHandler::long_press_triggered_ = false;
void (*ButtonHandler::callback_)(ClickType) = nullptr;
int ButtonHandler::button_state_ = HIGH;
bool ButtonHandler::short_press_pending_ = false;
bool ButtonHandler::long_press_in_progress_ = false;

void ButtonHandler::init() {
    pinMode(BUTTON_PIN, INPUT_PULLUP);
}

void ButtonHandler::setCallback(void (*callback)(ClickType)) {
    callback_ = callback;
}

void ButtonHandler::handle() {
    int reading = digitalRead(BUTTON_PIN);

    if (reading != button_state_) {
        unsigned long now = millis();

        if (reading == LOW) {
            last_press_time_ = now;
            long_press_triggered_ = false;
            long_press_in_progress_ = false;

            if (now - last_release_time_ > BUTTON_DEBOUNCE_MS) {
                click_count_++;
            }
        } else {
            last_release_time_ = now;

            if (!long_press_triggered_) {
                unsigned long press_duration = now - last_press_time_;
                if (press_duration < BUTTON_SINGLE_CLICK_MS) {
                    short_press_pending_ = true;
                }
            }
        }
    }

    button_state_ = reading;

    if (button_state_ == LOW && !long_press_triggered_) {
        unsigned long press_duration = millis() - last_press_time_;
        if (press_duration >= BUTTON_LONG_PRESS_MS) {
            long_press_triggered_ = true;
            long_press_in_progress_ = true;
            short_press_pending_ = false;
            if (callback_ != nullptr) {
                callback_(CLICK_LONG);
            }
        }
    }

    if (click_count_ > 0 && millis() - last_release_time_ > BUTTON_DOUBLE_CLICK_MS) {
        if (!long_press_in_progress_ && click_count_ >= 2 && callback_ != nullptr) {
            callback_(CLICK_DOUBLE);
        } else if (short_press_pending_ && callback_ != nullptr) {
            callback_(CLICK_SINGLE);
        }
        click_count_ = 0;
        short_press_pending_ = false;
        long_press_in_progress_ = false;
    }
}
