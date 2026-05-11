#include <ESP8266WiFi.h>
#include <SPI.h>
#include <SY7T609.h>

SY7T609 sensor;

const char* ssid = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";

WiFiClient wifiClient;

void setup() {
    Serial.begin(115200);
    delay(100);
    
    Serial.println();
    Serial.println(F("SY7T609 ESP8266 WiFi Monitor Example"));
    Serial.println(F("====================================="));
    
    SPISettings spiSettings(4000000, MSBFIRST, SPI_MODE3);
    uint8_t csPin = SY7T609_DEFAULT_CS_PIN;
    
    Serial.print(F("Initializing SY7T609... "));
    
    if (!sensor.begin(spiSettings, csPin, 5)) {
        Serial.println(F("FAILED!"));
        Serial.print(F("Error code: "));
        Serial.println(sensor.getLastError());
        while (1) {
            yield();
        }
    }
    
    Serial.println(F("OK"));
    Serial.print(F("Firmware: 0x"));
    Serial.println(sensor.getFWVersion(), HEX);
    
    Serial.println();
    Serial.print(F("Connecting to WiFi: "));
    Serial.println(ssid);
    
    WiFi.begin(ssid, password);
    
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
        delay(500);
        yield();
        Serial.print(F("."));
        attempts++;
    }
    
    if (WiFi.status() == WL_CONNECTED) {
        Serial.println(F(" OK"));
        Serial.print(F("IP address: "));
        Serial.println(WiFi.localIP());
    } else {
        Serial.println(F(" FAILED"));
        Serial.println(F("Will continue without WiFi"));
    }
    
    Serial.println();
    Serial.println(F("Starting energy monitoring..."));
    Serial.println();
}

void loop() {
    SY7T609::MeasurementData data;
    uint32_t startTime = micros();
    
    #ifdef ESP8266
    if (WiFi.status() == WL_CONNECTED) {
        while (millis() % 100 < 10) {
            yield();
        }
    }
    #endif
    
    bool success = sensor.readMeasurementFast(data);
    
    uint32_t elapsedTime = micros() - startTime;
    
    if (success) {
        Serial.print(F("["));
        Serial.print(millis());
        Serial.print(F("ms] "));
        
        Serial.print(F("VRMS="));
        Serial.print(data.vrms);
        Serial.print(F(" IRMS="));
        Serial.print(data.irms);
        Serial.print(F(" P="));
        Serial.print(data.power);
        
        if (data.vrms > 0 && data.irms > 0) {
            float powerFactor = (float)data.power / ((float)data.vrms * (float)data.irms / 1000.0);
            Serial.print(F(" PF="));
            Serial.print(powerFactor, 3);
        }
        
        Serial.print(F(" Freq="));
        Serial.print(data.frequency);
        Serial.print(F(" T="));
        Serial.print(data.temperature);
        
        Serial.print(F(" ["));
        Serial.print(elapsedTime);
        Serial.print(F("μs]"));
        
        Serial.println();
        
        #ifdef ESP8266
        if (WiFi.status() == WL_CONNECTED) {
            yield();
        }
        #endif
        
    } else {
        Serial.print(F("ERROR: "));
        Serial.print(sensor.getLastError());
        Serial.print(F(" Errors total: "));
        Serial.println(sensor.getErrorCount());
        
        sensor.clearErrorCounter();
    }
    
    delay(50);
    yield();
}
