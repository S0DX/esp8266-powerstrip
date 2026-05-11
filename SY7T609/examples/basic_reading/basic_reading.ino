#include <Arduino.h>
#include <SPI.h>
#include "SY7T609.h"

#define CS_PIN 10
#define SPI_SPEED 1000000

SY7T609 energyMeter;

void setup() {
    Serial.begin(115200);
    while (!Serial) {
        ;
    }

    SPI.begin();

    SPISettings settings(SPI_SPEED, MSBFIRST, SY7T609_SPI_MODE);
    if (!energyMeter.begin(settings, CS_PIN)) {
        Serial.println("Failed to initialize SY7T609!");
        while (1) {
        }
    }

    Serial.println("SY7T609 Energy Meter Initialized");
    Serial.print("Firmware Version: 0x");
    Serial.println(energyMeter.getFWVersion(), HEX);
    Serial.println();

    delay(1000);
}

void loop() {
    SY7T609::MeasurementData data = energyMeter.readMeasurement();

    Serial.println("========== Measurement Data ==========");
    Serial.print("Voltage RMS:  ");
    Serial.println(data.vrms);
    Serial.print("Current RMS:  ");
    Serial.println(data.irms);
    Serial.print("Active Power: ");
    Serial.println(data.power);
    Serial.print("Reactive Power: ");
    Serial.println(data.var);
    Serial.print("Apparent Power: ");
    Serial.println(data.va);
    Serial.print("Power Factor: ");
    Serial.println(data.pf);
    Serial.print("Frequency:    ");
    Serial.println(data.frequency);
    Serial.print("Temperature:  ");
    Serial.println(data.temperature);
    Serial.print("Voltage Avg:  ");
    Serial.println(data.vavg);
    Serial.print("Current Avg:  ");
    Serial.println(data.iavg);
    Serial.println();

    SY7T609::MinMaxData vMinMax;
    SY7T609::MinMaxData iMinMax;
    energyMeter.readVoltageMinMax(vMinMax);
    energyMeter.readCurrentMinMax(iMinMax);

    Serial.println("========== Min/Max Data ==========");
    Serial.print("Voltage - Min: ");
    Serial.print(vMinMax.lo);
    Serial.print("  Max: ");
    Serial.println(vMinMax.hi);
    Serial.print("Current - Min: ");
    Serial.print(iMinMax.lo);
    Serial.print("  Max: ");
    Serial.println(iMinMax.hi);
    Serial.println();

    SY7T609::PeakData peaks;
    energyMeter.readPeaks(peaks);
    Serial.println("========== Peak Data ==========");
    Serial.print("Voltage Peak: ");
    Serial.println(peaks.vpeak);
    Serial.print("Current Peak: ");
    Serial.println(peaks.ipeak);
    Serial.println();

    uint32_t alarms = energyMeter.readAlarms();
    if (alarms != 0) {
        Serial.println("========== Alarms ==========");
        Serial.print("Alarm Status: 0x");
        Serial.println(alarms, HEX);
        if (alarms & (1 << SY7T609::ALARM_OVERVOLT)) Serial.println("  - OVERVOLT alarm!");
        if (alarms & (1 << SY7T609::ALARM_UNDERVOLT)) Serial.println("  - UNDERVOLT alarm!");
        if (alarms & (1 << SY7T609::ALARM_OVERCURRENT)) Serial.println("  - OVERCURRENT alarm!");
        if (alarms & (1 << SY7T609::ALARM_OVERPOWER)) Serial.println("  - OVERPOWER alarm!");
        if (alarms & (1 << SY7T609::ALARM_OVERTEMP)) Serial.println("  - OVERTEMP alarm!");
        if (alarms & (1 << SY7T609::ALARM_UNDERTEMP)) Serial.println("  - UNDERTEMP alarm!");
        if (alarms & (1 << SY7T609::ALARM_OVERFREQ)) Serial.println("  - OVERFREQ alarm!");
        if (alarms & (1 << SY7T609::ALARM_UNDERFREQ)) Serial.println("  - UNDERFREQ alarm!");
        if (alarms & (1 << SY7T609::ALARM_VSAG)) Serial.println("  - VSAG alarm!");
        if (alarms & (1 << SY7T609::ALARM_VSURGE)) Serial.println("  - VSURGE alarm!");
        if (alarms & (1 << SY7T609::ALARM_VDROPOUT)) Serial.println("  - VDROPOUT alarm!");
        Serial.println();
    }

    delay(1000);
}