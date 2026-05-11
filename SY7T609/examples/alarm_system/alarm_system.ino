#include <Arduino.h>
#include <SPI.h>
#include "SY7T609.h"

#define CS_PIN 10
#define SPI_SPEED 1000000

#define LED_PIN 13

SY7T609 energyMeter;

bool ledState = false;
unsigned long lastAlarmCheck = 0;
const unsigned long ALARM_CHECK_INTERVAL = 100;

void setup() {
    Serial.begin(115200);
    while (!Serial) {
        ;
    }

    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);

    SPI.begin();

    SPISettings settings(SPI_SPEED, MSBFIRST, SY7T609_SPI_MODE);
    if (!energyMeter.begin(settings, CS_PIN)) {
        Serial.println("Failed to initialize SY7T609!");
        while (1) {
        }
    }

    energyMeter.setOverVoltageThreshold(140000);
    energyMeter.setUnderVoltageThreshold(90000);
    energyMeter.setOverCurrentThreshold(20000);
    energyMeter.setOverPowerThreshold(1000000);
    energyMeter.setOverTempThreshold(85000);
    energyMeter.setUnderTempThreshold(-10000);
    energyMeter.setOverFreqThreshold(6500);
    energyMeter.setUnderFreqThreshold(4500);
    energyMeter.setVoltageSagThreshold(100000);
    energyMeter.setVoltageSurgeThreshold(150000);

    energyMeter.setVoltageHoldTime(10);
    energyMeter.setCurrentHoldTime(10);
    energyMeter.setPowerHoldTime(10);
    energyMeter.setTemperatureHoldTime(10);
    energyMeter.setFrequencyHoldTime(10);

    energyMeter.setAlarmMask(SY7T609::DIO_1, 0x07F);

    Serial.println("SY7T609 Alarm System Started");
    Serial.print("Firmware Version: 0x");
    Serial.println(energyMeter.getFWVersion(), HEX);
    Serial.println();
    Serial.println("Alarm Thresholds Configured:");
    Serial.println("  - Over Voltage: 140000");
    Serial.println("  - Under Voltage: 90000");
    Serial.println("  - Over Current: 20000");
    Serial.println("  - Over Power: 1000000");
    Serial.println("  - Over Temp: 85 C");
    Serial.println("  - Under Temp: -10 C");
    Serial.println("  - Over Freq: 65 Hz");
    Serial.println("  - Under Freq: 45 Hz");
    Serial.println();

    delay(1000);
}

void handleAlarms() {
    uint32_t alarms = energyMeter.readAlarms();

    if (alarms & (1 << SY7T609::ALARM_OVERVOLT)) {
        Serial.println("ALARM: Over Voltage!");
        digitalWrite(LED_PIN, HIGH);
        ledState = true;
    }
    if (alarms & (1 << SY7T609::ALARM_UNDERVOLT)) {
        Serial.println("ALARM: Under Voltage!");
        digitalWrite(LED_PIN, HIGH);
        ledState = true;
    }
    if (alarms & (1 << SY7T609::ALARM_OVERCURRENT)) {
        Serial.println("ALARM: Over Current!");
        digitalWrite(LED_PIN, HIGH);
        ledState = true;
    }
    if (alarms & (1 << SY7T609::ALARM_OVERPOWER)) {
        Serial.println("ALARM: Over Power!");
        digitalWrite(LED_PIN, HIGH);
        ledState = true;
    }
    if (alarms & (1 << SY7T609::ALARM_OVERTEMP)) {
        Serial.println("ALARM: Over Temperature!");
        digitalWrite(LED_PIN, HIGH);
        ledState = true;
    }
    if (alarms & (1 << SY7T609::ALARM_UNDERTEMP)) {
        Serial.println("ALARM: Under Temperature!");
        digitalWrite(LED_PIN, HIGH);
        ledState = true;
    }
    if (alarms & (1 << SY7T609::ALARM_OVERFREQ)) {
        Serial.println("ALARM: Over Frequency!");
        digitalWrite(LED_PIN, HIGH);
        ledState = true;
    }
    if (alarms & (1 << SY7T609::ALARM_UNDERFREQ)) {
        Serial.println("ALARM: Under Frequency!");
        digitalWrite(LED_PIN, HIGH);
        ledState = true;
    }
    if (alarms & (1 << SY7T609::ALARM_VSAG)) {
        Serial.println("ALARM: Voltage Sag!");
        digitalWrite(LED_PIN, HIGH);
        ledState = true;
    }
    if (alarms & (1 << SY7T609::ALARM_VSURGE)) {
        Serial.println("ALARM: Voltage Surge!");
        digitalWrite(LED_PIN, HIGH);
        ledState = true;
    }
    if (alarms & (1 << SY7T609::ALARM_VDROPOUT)) {
        Serial.println("ALARM: Voltage Dropout!");
        digitalWrite(LED_PIN, HIGH);
        ledState = true;
    }

    if (alarms == 0 && ledState) {
        digitalWrite(LED_PIN, LOW);
        ledState = false;
    }
}

void printAlarmCounts() {
    Serial.println("========== Alarm Event Counts ==========");
    Serial.print("Under Temp Count:    ");
    Serial.println(energyMeter.getAlarmCount(SY7T609::ALARM_UNDERTEMP));
    Serial.print("Over Temp Count:     ");
    Serial.println(energyMeter.getAlarmCount(SY7T609::ALARM_OVERTEMP));
    Serial.print("Under Volt Count:    ");
    Serial.println(energyMeter.getAlarmCount(SY7T609::ALARM_UNDERVOLT));
    Serial.print("Over Volt Count:     ");
    Serial.println(energyMeter.getAlarmCount(SY7T609::ALARM_OVERVOLT));
    Serial.print("Over Current Count:  ");
    Serial.println(energyMeter.getAlarmCount(SY7T609::ALARM_OVERCURRENT));
    Serial.print("Over Power Count:    ");
    Serial.println(energyMeter.getAlarmCount(SY7T609::ALARM_OVERPOWER));
    Serial.print("Under Freq Count:    ");
    Serial.println(energyMeter.getAlarmCount(SY7T609::ALARM_UNDERFREQ));
    Serial.print("Over Freq Count:     ");
    Serial.println(energyMeter.getAlarmCount(SY7T609::ALARM_OVERFREQ));
    Serial.print("Voltage Sag Count:   ");
    Serial.println(energyMeter.getAlarmCount(SY7T609::ALARM_VSAG));
    Serial.print("Voltage Surge Count: ");
    Serial.println(energyMeter.getAlarmCount(SY7T609::ALARM_VSURGE));
    Serial.println();
}

void loop() {
    unsigned long currentMillis = millis();

    if (currentMillis - lastAlarmCheck >= ALARM_CHECK_INTERVAL) {
        lastAlarmCheck = currentMillis;
        handleAlarms();
    }

    SY7T609::MeasurementData data = energyMeter.readMeasurement();

    static unsigned long lastPrintTime = 0;
    if (currentMillis - lastPrintTime >= 5000) {
        lastPrintTime = currentMillis;

        Serial.println("========== Real-time Measurements ==========");
        Serial.print("Voltage RMS: ");
        Serial.println(data.vrms);
        Serial.print("Current RMS: ");
        Serial.println(data.irms);
        Serial.print("Active Power: ");
        Serial.println(data.power);
        Serial.print("Temperature: ");
        Serial.println(data.temperature);
        Serial.print("Frequency: ");
        Serial.println(data.frequency);
        Serial.println();

        printAlarmCounts();
    }

    if (Serial.available() > 0) {
        char cmd = Serial.read();
        if (cmd == 'c' || cmd == 'C') {
            Serial.println("Clearing all alarms...");
            for (int i = 0; i <= 10; i++) {
                energyMeter.clearAlarm((SY7T609::AlarmType)i);
            }
            digitalWrite(LED_PIN, LOW);
            ledState = false;
            Serial.println("Alarms cleared");
        }
        else if (cmd == 'r' || cmd == 'R') {
            Serial.println("Resetting Min/Max values...");
            energyMeter.resetMinMax();
            Serial.println("Min/Max values reset");
        }
        else if (cmd == 's' || cmd == 'S') {
            Serial.println("Saving configuration to Flash...");
            energyMeter.saveToFlash();
            Serial.println("Configuration saved");
        }
    }

    delay(10);
}