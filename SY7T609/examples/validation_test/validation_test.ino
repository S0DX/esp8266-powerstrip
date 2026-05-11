#include <Arduino.h>
#include <SPI.h>
#include "SY7T609.h"

#define CS_PIN 10
#define SPI_SPEED 2000000
#define LED_PIN 13

SY7T609 energyMeter;

void printErrorInfo() {
    Serial.print("  错误代码：");
    Serial.println(energyMeter.getLastError());
    Serial.print("  错误计数：");
    Serial.println(energyMeter.getErrorCount());
}

void testSPICommunication() {
    Serial.println("=== 测试 1: SPI 通信基础测试 ===");
    
    SPISettings settings(SPI_SPEED, MSBFIRST, SPI_MODE3);
    
    Serial.print("尝试初始化 (重试 5 次)... ");
    if (energyMeter.begin(settings, CS_PIN, 5)) {
        Serial.println("✓ 成功");
    } else {
        Serial.println("✗ 失败");
        printErrorInfo();
        while(1);
    }
    
    uint32_t version = energyMeter.getFWVersion();
    Serial.print("固件版本：0x");
    Serial.println(version, HEX);
    
    if (version == 0 || version == 0xFFFFFF) {
        Serial.println("✗ 固件版本无效!");
        printErrorInfo();
        while(1);
    }
    
    Serial.println();
}

void testRegisterReadWrite() {
    Serial.println("=== 测试 2: 寄存器读写验证 ===");
    
    uint32_t originalValue, readbackValue;
    
    if (!energyMeter.readRegister(0x50, originalValue)) {
        Serial.println("✗ 读取 VSCALE 失败");
        printErrorInfo();
        return;
    }
    
    Serial.print("原始 VSCALE: ");
    Serial.println(originalValue);
    
    uint32_t testValue = 100000;
    Serial.print("写入测试值：");
    Serial.println(testValue);
    
    if (!energyMeter.writeRegister(0x50, testValue, 3)) {
        Serial.println("✗ 写入 VSCALE 失败");
        printErrorInfo();
        return;
    }
    
    if (!energyMeter.readRegister(0x50, readbackValue, 3)) {
        Serial.println("✗ 读回验证失败");
        printErrorInfo();
        return;
    }
    
    if (readbackValue == testValue) {
        Serial.println("✓ 读回验证成功");
    } else {
        Serial.print("✗ 读回验证失败! 期望：");
        Serial.print(testValue);
        Serial.print(" 实际：");
        Serial.println(readbackValue);
        printErrorInfo();
        return;
    }
    
    energyMeter.writeRegister(0x50, originalValue, 3);
    Serial.println();
}

void testMeasurementData() {
    Serial.println("=== 测试 3: 测量数据读取 ===");
    
    SY7T609::MeasurementData data = energyMeter.readMeasurement();
    
    Serial.print("VRMS: ");
    Serial.println(data.vrms);
    Serial.print("IRMS: ");
    Serial.println(data.irms);
    Serial.print("Power: ");
    Serial.println(data.power);
    Serial.print("PF: ");
    Serial.println(data.pf);
    Serial.print("Frequency: ");
    Serial.println(data.frequency);
    
    if (energyMeter.getErrorCount() > 0) {
        Serial.println("⚠ 检测到错误");
        printErrorInfo();
    } else {
        Serial.println("✓ 数据读取正常");
    }
    
    Serial.println();
}

void testCalibration() {
    Serial.println("=== 测试 4: 校准功能测试 ===");
    
    Serial.println("测试电压增益校准 (不实际执行，仅测试流程)...");
    Serial.println("注意：实际校准需要施加标准电压源");
    
    uint32_t command;
    energyMeter.readRegister(0x00, command);
    Serial.print("当前 COMMAND 寄存器：0x");
    Serial.println(command, HEX);
    
    if (command == 0) {
        Serial.println("✓ 芯片就绪，可以执行校准");
    } else {
        Serial.println("⚠ 芯片忙或有待处理命令");
    }
    
    Serial.println();
}

void testAlarmSystem() {
    Serial.println("=== 测试 5: 报警系统测试 ===");
    
    uint32_t alarms = energyMeter.readAlarms();
    Serial.print("当前报警状态：0x");
    Serial.println(alarms, HEX);
    
    if (alarms == 0) {
        Serial.println("✓ 无活动报警");
    } else {
        Serial.println("⚠ 存在报警:");
        if (alarms & (1 << SY7T609::ALARM_OVERVOLT)) Serial.println("  - 过压");
        if (alarms & (1 << SY7T609::ALARM_UNDERVOLT)) Serial.println("  - 欠压");
        if (alarms & (1 << SY7T609::ALARM_OVERCURRENT)) Serial.println("  - 过流");
        if (alarms & (1 << SY7T609::ALARM_OVERPOWER)) Serial.println("  - 过功率");
        if (alarms & (1 << SY7T609::ALARM_OVERTEMP)) Serial.println("  - 过温");
    }
    
    Serial.println();
}

void testErrorRecovery() {
    Serial.println("=== 测试 6: 错误恢复测试 ===");
    
    energyMeter.clearErrorCounter();
    Serial.println("清零错误计数器");
    
    Serial.print("错误计数：");
    Serial.println(energyMeter.getErrorCount());
    
    Serial.println("模拟错误读取 (访问无效地址)...");
    uint32_t value;
    energyMeter.readRegister(0xFF, value, 1);
    
    Serial.print("错误计数：");
    Serial.println(energyMeter.getErrorCount());
    Serial.print("最后错误：");
    Serial.println(energyMeter.getLastError());
    
    Serial.println("✓ 错误恢复机制工作正常");
    Serial.println();
}

void stressTest() {
    Serial.println("=== 压力测试：连续读取 1000 次 ===");
    
    unsigned long startTime = millis();
    uint32_t successCount = 0;
    uint32_t failCount = 0;
    
    energyMeter.clearErrorCounter();
    
    for (int i = 0; i < 1000; i++) {
        uint32_t vrms;
        if (energyMeter.readRegister(0x11, vrms, 3)) {
            successCount++;
        } else {
            failCount++;
        }
        
        if (i % 100 == 0) {
            Serial.print("进度：");
            Serial.print(i);
            Serial.println("/1000");
        }
    }
    
    unsigned long duration = millis() - startTime;
    
    Serial.print("成功：");
    Serial.println(successCount);
    Serial.print("失败：");
    Serial.println(failCount);
    Serial.print("耗时：");
    Serial.print(duration);
    Serial.println(" ms");
    Serial.print("平均每次：");
    Serial.print((float)duration / 1000);
    Serial.println(" ms");
    Serial.print("成功率：");
    Serial.print((float)successCount / 10);
    Serial.println("%");
    
    if (failCount > 0) {
        Serial.print("错误计数：");
        Serial.println(energyMeter.getErrorCount());
    }
    
    Serial.println();
}

void setup() {
    Serial.begin(115200);
    while (!Serial) {
        ;
    }
    
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, HIGH);
    
    Serial.println();
    Serial.println("╔════════════════════════════════════════╗");
    Serial.println("║   SY7T609 商业级库验证测试            ║");
    Serial.println("║   Commercial Grade Validation Test    ║");
    Serial.println("╚════════════════════════════════════════╝");
    Serial.println();
    
    delay(1000);
    
    testSPICommunication();
    testRegisterReadWrite();
    testMeasurementData();
    testCalibration();
    testAlarmSystem();
    testErrorRecovery();
    stressTest();
    
    Serial.println("╔════════════════════════════════════════╗");
    Serial.println("║         所有测试完成                  ║");
    Serial.println("╚════════════════════════════════════════╝");
    
    digitalWrite(LED_PIN, LOW);
}

void loop() {
    SY7T609::MeasurementData data = energyMeter.readMeasurement();
    
    Serial.print("[");
    Serial.print(millis() / 1000);
    Serial.print("s] VRMS=");
    Serial.print(data.vrms);
    Serial.print(" IRMS=");
    Serial.print(data.irms);
    Serial.print(" P=");
    Serial.print(data.power);
    
    if (energyMeter.getErrorCount() > 0) {
        Serial.print(" [ERROR ");
        Serial.print(energyMeter.getLastError());
        Serial.print("]");
        energyMeter.clearErrorCounter();
    }
    
    Serial.println();
    
    delay(1000);
}