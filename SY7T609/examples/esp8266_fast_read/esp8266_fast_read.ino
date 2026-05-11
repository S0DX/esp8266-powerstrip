#include <ESP8266WiFi.h>
#include <SPI.h>
#include <SY7T609.h>

SY7T609 sensor;

void setup() {
    Serial.begin(115200);
    delay(100);
    
    Serial.println();
    Serial.println(F("SY7T609 ESP8266 Fast Read Example"));
    Serial.println(F("=================================="));
    
    SPISettings spiSettings(4000000, MSBFIRST, SPI_MODE3);
    uint8_t csPin = SY7T609_DEFAULT_CS_PIN;
    
    Serial.print(F("Initializing SY7T609 on CS pin "));
    Serial.println(csPin);
    
    if (!sensor.begin(spiSettings, csPin, 5)) {
        Serial.println(F("ERROR: Failed to initialize SY7T609!"));
        Serial.print(F("Last error: "));
        Serial.println(sensor.getLastError());
        while (1) {
            yield();
        }
    }
    
    Serial.println(F("SY7T609 initialized successfully!"));
    Serial.print(F("Firmware version: 0x"));
    Serial.println(sensor.getFWVersion(), HEX);
    
    Serial.println();
    Serial.println(F("Starting measurement loop..."));
    Serial.println(F("Press any key to stop"));
    Serial.println();
}

void loop() {
    SY7T609::MeasurementData data;
    uint32_t startTime = micros();
    
    bool success = sensor.readMeasurementFast(data);
    
    uint32_t elapsedTime = micros() - startTime;
    
    if (success) {
        Serial.print(F("Time: "));
        Serial.print(elapsedTime);
        Serial.print(F(" μs | "));
        
        Serial.print(F("VRMS: "));
        Serial.print(data.vrms);
        Serial.print(F(" | "));
        
        Serial.print(F("IRMS: "));
        Serial.print(data.irms);
        Serial.print(F(" | "));
        
        Serial.print(F("Power: "));
        Serial.print(data.power);
        Serial.print(F(" | "));
        
        Serial.print(F("Freq: "));
        Serial.print(data.frequency);
        Serial.print(F(" | "));
        
        Serial.print(F("Temp: "));
        Serial.print(data.temperature);
        Serial.println();
    } else {
        Serial.print(F("ERROR: Read failed! Error code: "));
        Serial.println(sensor.getLastError());
    }
    
    if (Serial.available()) {
        Serial.read();
        Serial.println(F("Stopped by user"));
        while (1) {
            yield();
        }
    }
    
    delay(100);
    yield();
}
