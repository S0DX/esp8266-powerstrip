#include "button_mgr.h"
#include "config.h"

unsigned long ButtonHandler::last_release_time_ = 0;
unsigned long ButtonHandler::debounce_start_time_ = 0;
void (*ButtonHandler::callback_)(ButtonHandler::ClickType) = nullptr;
int ButtonHandler::button_state_ = HIGH;

void ButtonHandler::init() {
    pinMode(BUTTON_PIN, INPUT_PULLUP);
}

void ButtonHandler::setCallback(void (*callback)(ClickType)) {
    callback_ = callback;
}

void ButtonHandler::handle() {
    int reading = digitalRead(BUTTON_PIN);
    unsigned long now = millis();

    if (reading != button_state_) {
        // 状态发生变化，启动去抖计时
        if (debounce_start_time_ == 0) {
            debounce_start_time_ = now;
        }
        if (now - debounce_start_time_ >= BUTTON_DEBOUNCE_MS) {
            // 新状态稳定超过去抖时间，接受状态变更
            button_state_ = reading;
            debounce_start_time_ = 0;

            if (button_state_ == LOW) {
                // 按下：再检查与上次释放的间隔，避免释放后的抖动被误判为新的按下
                if (now - last_release_time_ > BUTTON_DEBOUNCE_MS) {
                    if (callback_ != nullptr) {
                        callback_(CLICK_PRESS);
                    }
                }
            } else {
                // 释放
                last_release_time_ = now;
                if (callback_ != nullptr) {
                    callback_(CLICK_RELEASE);
                }
            }
        }
    } else {
        // 状态稳定，重置去抖计时
        debounce_start_time_ = 0;
    }
}
