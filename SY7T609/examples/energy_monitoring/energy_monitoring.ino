#include <Arduino.h>
#include <SPI.h>
#include "SY7T609.h"

#define CS_PIN 10
#define SPI_SPEED 1000000

#define VSCALE_VALUE 667000
#define ISCALE_VALUE 8000000
#define PSCALE_VALUE 8337500
#define FSCALE_VALUE 100
#define PFSCALE_VALUE 1000

SY7T609 energyMeter;

float convertVoltage(uint32_t rawValue) {
    return (float)rawValue / VSCALE_VALUE * 120.0;
}

float convertCurrent(uint32_t rawValue) {
    return (float)rawValue / ISCALE_VALUE * 62.5;
}

float convertPower(int32_t rawValue) {
    return (float)rawValue / PSCALE_VALUE * 41687.5;
}

float convertFrequency(uint32_t rawValue) {
    return (float)rawValue / FSCALE_VALUE;
}

float convertPF(int32_t rawValue) {
    return (float)rawValue / PFSCALE_VALUE;
}

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

    energyMeter.setVSCALE(VSCALE_VALUE);
    energyMeter.setISCALE(ISCALE_VALUE);
    energyMeter.setPSCALE(PSCALE_VALUE);
    energyMeter.setFSCALE(FSCALE_VALUE);
    energyMeter.setPFSCALE(PFSCALE_VALUE);

    uint32_t bucketH = 0x000247;
    uint32_t bucketL = 0x6E4F76;
    energyMeter.setBucketSize(bucketH, bucketL);

    Serial.println("SY7T609 Energy Monitoring Started");
    Serial.print("Firmware Version: 0x");
    Serial.println(energyMeter.getFWVersion(), HEX);
    Serial.println();
    Serial.println("Note: Energy counters are cleared at startup");
    energyMeter.clearEnergyCounters();
    Serial.println();

    delay(1000);
}

void loop() {
    SY7T609::MeasurementData data = energyMeter.readMeasurement();
    SY7T609::EnergyData energy;
    energyMeter.readEnergyCounters(energy);

    float voltage = convertVoltage(data.vrms);
    float current = convertCurrent(data.irms);
    float power = convertPower(data.power);
    float freq = convertFrequency(data.frequency);
    float pf = convertPF(data.pf);

    Serial.println("========== Real-time Measurements ==========");
    Serial.print("Voltage: ");
    Serial.print(voltage, 2);
    Serial.println(" V");
    Serial.print("Current: ");
    Serial.print(current, 4);
    Serial.println(" A");
    Serial.print("Active Power: ");
    Serial.print(power, 2);
    Serial.println(" W");
    Serial.print("Frequency: ");
    Serial.print(freq, 2);
    Serial.println(" Hz");
    Serial.print("Power Factor: ");
    Serial.println(pf, 3);
    Serial.println();

    Serial.println("========== Energy Counters ==========");
    Serial.print("Positive Active Energy (EPPcnt): ");
    Serial.println(energy.eppcnt);
    Serial.print("Negative Active Energy (EPMcnt): ");
    Serial.println(energy.epmcnt);
    Serial.print("Net Active Energy (EPNcnt): ");
    Serial.println(energy.epncnt);
    Serial.print("Net Reactive Energy (EQNcnt): ");
    Serial.println(energy.eqncnt);
    Serial.print("Net Apparent Energy (ESNcnt): ");
    Serial.println(energy.esncnt);
    Serial.println();

    float energy_wh = (float)energy.eppcnt * 3600.0 * 6702.0 / (62.5 * 667.0);
    Serial.print("Calculated Energy (Wh): ");
    Serial.println(energy_wh, 4);

    delay(2000);
}

uint32_t convertToRawVoltage(float voltage) {
    return (uint32_t)(voltage / 120.0 * VSCALE_VALUE);
}

uint32_t convertToRawCurrent(float current) {
    return (uint32_t)(current / 62.5 * ISCALE_VALUE);
}