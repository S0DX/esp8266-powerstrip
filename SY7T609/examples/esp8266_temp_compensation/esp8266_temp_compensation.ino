#include <ESP8266WiFi.h>
#include <SPI.h>
#include <SY7T609.h>

SY7T609 sensor;

float tempCoeff_Voltage = 0.0001;
float tempCoeff_Current = 0.0001;
float tempCoeff_Power = 0.0002;

void setup() {
    Serial.begin(115200);
    delay(100);
    
    Serial.println();
    Serial.println(F("SY7T609 ESP8266 Temperature Compensation Example"));
    Serial.println(F("================================================"));
    
    SPISettings spiSettings(4000000, MSBFIRST, SPI_MODE3);
    uint8_t csPin = SY7T609_DEFAULT_CS_PIN;
    
    Serial.print(F("Initializing SY7T609... "));
    
    if (!sensor.begin(spiSettings, csPin, 5)) {
        Serial.println(F("FAILED!"));
        Serial.print(F("Error: "));
        Serial.println(sensor.getLastError());
        while (1) yield();
    }
    
    Serial.println(F("OK"));
    Serial.print(F("Firmware: 0x"));
    Serial.println(sensor.getFWVersion(), HEX);
    
    Serial.println();
    Serial.println(F("Temperature compensation enabled"));
    Serial.print(F("  Voltage coefficient: "));
    Serial.println(tempCoeff_Voltage, 5);
    Serial.print(F("  Current coefficient: "));
    Serial.println(tempCoeff_Current, 5);
    Serial.print(F("  Power coefficient: "));
    Serial.println(tempCoeff_Power, 5);
    Serial.println();
    
    Serial.println(F("Starting compensated measurements..."));
    Serial.println();
}

uint32_t readVRMS_Compensated() {
    uint32_t rawVRMS = sensor.readVRMS();
    uint32_t temp = sensor.readTemperature();
    
    float tempCelsius = ((float)temp / 1000.0) - 40.0;
    float tempDelta = tempCelsius - 25.0;
    
    float compensated = rawVRMS * (1.0 + tempCoeff_Voltage * tempDelta);
    
    return (uint32_t)compensated;
}

uint32_t readIRMS_Compensated() {
    uint32_t rawIRMS = sensor.readIRMS();
    uint32_t temp = sensor.readTemperature();
    
    float tempCelsius = ((float)temp / 1000.0) - 40.0;
    float tempDelta = tempCelsius - 25.0;
    
    float compensated = rawIRMS * (1.0 + tempCoeff_Current * tempDelta);
    
    return (uint32_t)compensated;
}

int32_t readPower_Compensated() {
    int32_t rawPower = sensor.readPower();
    uint32_t temp = sensor.readTemperature();
    
    float tempCelsius = ((float)temp / 1000.0) - 40.0;
    float tempDelta = tempCelsius - 25.0;
    
    float compensated = rawPower * (1.0 + tempCoeff_Power * tempDelta);
    
    return (int32_t)compensated;
}

void loop() {
    uint32_t vrms_comp = readVRMS_Compensated();
    uint32_t irms_comp = readIRMS_Compensated();
    int32_t power_comp = readPower_Compensated();
    
    uint32_t temp = sensor.readTemperature();
    float tempCelsius = ((float)temp / 1000.0) - 40.0;
    
    Serial.print(F("Temp: "));
    Serial.print(tempCelsius, 1);
    Serial.print(F("°C | "));
    
    Serial.print(F("VRMS: "));
    Serial.print(vrms_comp);
    Serial.print(F(" (compensated) | "));
    
    Serial.print(F("IRMS: "));
    Serial.print(irms_comp);
    Serial.print(F(" (compensated) | "));
    
    Serial.print(F("Power: "));
    Serial.print(power_comp);
    Serial.print(F(" (compensated)"));
    
    Serial.println();
    
    delay(200);
    yield();
}
