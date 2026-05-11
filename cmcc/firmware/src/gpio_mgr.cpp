#include "gpio_mgr.h"
#include "config.h"

bool GPIOManager::relay_master_state_ = false;
bool GPIOManager::relay_slave_state_ = false;
bool GPIOManager::locked_ = false;
GPIOManager::LEDMode GPIOManager::blue_mode_ = LED_OFF;
GPIOManager::LEDMode GPIOManager::red_mode_ = LED_OFF;
unsigned long GPIOManager::blink_timer_ = 0;
bool GPIOManager::blink_state_ = false;

#define EEPROM_LOCK_ADDR    200
#define EEPROM_MAGIC_LOCK   0x42

void GPIOManager::init() {
    initRelayPins();
    initLEDPins();
    initButtonPin();

    EEPROM.begin(256);
    if (EEPROM.read(EEPROM_LOCK_ADDR) == EEPROM_MAGIC_LOCK) {
        locked_ = true;
        Serial.println("[GPIO] Lock mode enabled");
    }
}

void GPIOManager::initRelayPins() {
    pinMode(RELAY_MASTER_PIN, OUTPUT);
    pinMode(RELAY_SLAVE_PIN, OUTPUT);
    pinMode(RELAY_ENABLE_PIN, OUTPUT);

    digitalWrite(RELAY_MASTER_PIN, HIGH);
    digitalWrite(RELAY_SLAVE_PIN, HIGH);
    digitalWrite(RELAY_ENABLE_PIN, HIGH);

    relay_master_state_ = false;
    relay_slave_state_ = false;
}

void GPIOManager::initLEDPins() {
    pinMode(LED_BLUE_PIN, OUTPUT);
    pinMode(LED_RED_PIN, OUTPUT);
    pinMode(LED_WHITE_PIN, OUTPUT);

    digitalWrite(LED_BLUE_PIN, HIGH);
    digitalWrite(LED_RED_PIN, HIGH);
    digitalWrite(LED_WHITE_PIN, HIGH);
}

void GPIOManager::initButtonPin() {
    pinMode(BUTTON_PIN, INPUT_PULLUP);
}

void GPIOManager::setRelayMaster(bool on) {
    if (locked_) {
        Serial.println("[GPIO] Locked: ignoring setRelayMaster");
        return;
    }

    relay_master_state_ = on;
    digitalWrite(RELAY_MASTER_PIN, on ? LOW : HIGH);
    triggerRelayEnable();

    if (!on) {
        relay_slave_state_ = false;
        digitalWrite(RELAY_SLAVE_PIN, HIGH);
        triggerRelayEnable();
        setLEDWhite(false);
    }
}

void GPIOManager::setRelaySlave(bool on) {
    if (locked_) {
        Serial.println("[GPIO] Locked: ignoring setRelaySlave");
        return;
    }

    if (on && !relay_master_state_) {
        setRelayMaster(true);
    }

    relay_slave_state_ = on;
    digitalWrite(RELAY_SLAVE_PIN, on ? LOW : HIGH);
    triggerRelayEnable();

    setLEDWhite(on && relay_master_state_);
}

void GPIOManager::setLocked(bool locked) {
    locked_ = locked;
    EEPROM.write(EEPROM_LOCK_ADDR, locked ? EEPROM_MAGIC_LOCK : 0);
    EEPROM.commit();

    if (locked) {
        relay_master_state_ = false;
        relay_slave_state_ = false;
        digitalWrite(RELAY_MASTER_PIN, HIGH);
        digitalWrite(RELAY_SLAVE_PIN, HIGH);
        digitalWrite(RELAY_ENABLE_PIN, HIGH);
        setLEDWhite(false);
        Serial.println("[GPIO] Locked: all relays off");
    } else {
        Serial.println("[GPIO] Unlocked");
    }
}

bool GPIOManager::isLocked() {
    return locked_;
}

void GPIOManager::triggerRelayEnable() {
    digitalWrite(RELAY_ENABLE_PIN, LOW);
    delay(RELAY_ENABLE_PULSE_MS);
    digitalWrite(RELAY_ENABLE_PIN, HIGH);
}

bool GPIOManager::getRelayMaster() {
    return relay_master_state_;
}

bool GPIOManager::getRelaySlave() {
    return relay_slave_state_;
}

void GPIOManager::setLEDBlue(bool on, bool blink) {
    digitalWrite(LED_BLUE_PIN, on ? LOW : HIGH);
    if (blink) {
        blue_mode_ = LED_BLINK;
    } else {
        blue_mode_ = on ? LED_ON : LED_OFF;
    }
}

void GPIOManager::setLEDRed(bool on, bool blink) {
    digitalWrite(LED_RED_PIN, on ? LOW : HIGH);
    if (blink) {
        red_mode_ = LED_BLINK;
    } else {
        red_mode_ = on ? LED_ON : LED_OFF;
    }
}

void GPIOManager::setLEDWhite(bool on) {
    digitalWrite(LED_WHITE_PIN, on ? LOW : HIGH);
}

void GPIOManager::setBlueMode(LEDMode mode) {
    blue_mode_ = mode;
}

void GPIOManager::setRedMode(LEDMode mode) {
    red_mode_ = mode;
}

void GPIOManager::updateLEDs() {
    if (blue_mode_ == LED_BLINK || red_mode_ == LED_BLINK) {
        if (millis() - blink_timer_ > 500) {
            blink_timer_ = millis();
            blink_state_ = !blink_state_;

            if (blue_mode_ == LED_BLINK) {
                digitalWrite(LED_BLUE_PIN, blink_state_ ? LOW : HIGH);
            }
            if (red_mode_ == LED_BLINK) {
                digitalWrite(LED_RED_PIN, blink_state_ ? LOW : HIGH);
            }
        }
    }
}
