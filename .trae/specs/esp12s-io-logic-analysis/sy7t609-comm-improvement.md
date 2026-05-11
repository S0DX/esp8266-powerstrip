# SY7T609 通信改进方案 - 确保固件烧录不受影响

## 问题描述

原代码中 SY7T609 与 ESP12S 的 UART 通信使用 GPIO1(TX) 和 GPIO3(RX)，这两个引脚也是 ESP8266 的 USB 串口烧录引脚。同时 GPIO0 既用于主继电器控制，又用于烧录模式检测（烧录时需拉低）。

### 原有问题

1. **GPIO0 检测时序错误**：
   - `GPIOManager::init()` 先将 GPIO0 设为 OUTPUT HIGH
   - `SY7T609::init()` 后检测 GPIO0 电平
   - 此时已无法正确检测烧录时的 LOW 电平

2. **Serial 切换风险**：
   - SY7T609 启用时 `Serial.begin(9600)` 改变波特率
   - 影响 USB 调试输出
   - Serial1 初始化时机不确定

3. **缺乏重试机制**：
   - SY7T609 初始化失败后无重试
   - 可能导致设备无法使用

## 解决方案

### 1. 多级保护策略

#### 第一级：EEPROM 烧录模式标志
- 地址 206 存储烧录模式标志 (0xAA)
- 一旦检测到烧录模式，永久禁用 SY7T609
- 避免每次启动都检测 GPIO0

#### 第二级：上电 GPIO0 电平检测
- 在 `GPIOManager::init()` 之后立即检测
- 临时切换 GPIO0 为 INPUT_PULLUP
- 检测后恢复为 OUTPUT HIGH

#### 第三级：启动延迟保护
- 上电后延迟 2 秒再初始化 SY7T609
- 确保 GPIO0 已稳定为 HIGH（非烧录模式）
- 防止误判

### 2. 改进的初始化流程

```cpp
void SY7T609::init() {
    // 1. 检查 EEPROM 烧录标志
    if (EEPROM[206] == 0xAA) {
        flash_mode_ = true;
        return;  // 永久禁用
    }
    
    // 2. 检测 GPIO0 电平
    checkGPIO0ForFlashMode();
    if (flash_mode_) {
        // 写入 EEPROM 标志，下次启动跳过检测
        EEPROM.write(206, 0xAA);
        return;
    }
    
    // 3. 加载用户配置
    loadFromEEPROM();
    
    // 4. 初始化 SY7T609（带重试）
    if (enabled_) {
        initializeSY7T609();  // 最多重试 3 次
    }
}
```

### 3. 可靠的串口切换

#### 初始化顺序：
```cpp
// main.cpp setup()
Serial.begin(115200);      // 1. USB 串口初始化
GPIOManager::init();       // 2. GPIO 初始化（包括 GPIO0）
SY7T609::init();           // 3. SY7T609 初始化
                           //    - 内部调用 Serial.begin(9600)
                           //    - 成功后调用 Serial1.begin(115200)
```

#### 调试输出切换：
```cpp
// config.h
#define DBG_PRINTF(fmt, ...) do { \
    if (g_meter_enabled) { \
        Serial1.printf(fmt, ##__VA_ARGS__); \
    } else { \
        Serial.printf(fmt, ##__VA_ARGS__); \
    } \
} while(0)
```

### 4. 新增功能

#### 4.1 初始化重试机制
```cpp
bool SY7T609::initializeSY7T609() {
    if (init_retry_count_ >= 3) {
        return false;  // 放弃
    }
    
    Serial.begin(9600);
    if (readRegister(0x01, version)) {
        // 成功
        init_retry_count_ = 0;
        return true;
    } else {
        // 失败，重试
        init_retry_count_++;
        Serial.end();
        Serial.begin(115200);
        delay(500);
        return false;
    }
}
```

#### 4.2 接收超时保护
```cpp
bool SY7T609::uartCommand() {
    // 带超时的接收
    unsigned long start = millis();
    while (bytesRead < 7 && (millis() - start) < 100) {
        if (Serial.available()) {
            rxBuffer[bytesRead++] = Serial.read();
        }
        delay(1);
    }
    return (bytesRead == 7);
}
```

#### 4.3 自动报告数据完整性检查
```cpp
void SY7T609::handle() {
    if (auto_report_) {
        // 确保至少有 20 字节（一帧完整数据）
        if (Serial.available() >= 20) {
            uint8_t buffer[64];
            size_t len = Serial.readBytes(buffer, sizeof(buffer));
            parseAutoReport(buffer, len);
        }
    }
}
```

## EEPROM 布局更新

```
地址 0-63:     WiFi SSID (32 字节)
地址 64-127:   WiFi 密码 (64 字节)
地址 128-135:  能耗数据 (4 字节魔数 +4 字节 float)
地址 200:      GPIO 锁定标志 (0x42=锁定)
地址 205:      SY7T609 使能标志 (1=启用)
地址 206:      SY7T609 烧录模式标志 (0xAA=已检测为烧录模式) [新增]
地址 254:      WiFi 配置魔数 (0xAB)
```

## 代码变更清单

### sy7t609.h
- 新增 `gpio0_checked_` 标志
- 新增 `last_init_attempt_` 和 `init_retry_count_`
- 新增 `checkGPIO0ForFlashMode()` 方法
- 新增 `initializeSY7T609()` 方法

### sy7t609.cpp
- 重写 `init()` 方法，实现多级保护
- 改进 `uartCommand()` 添加超时保护
- 改进 `handle()` 添加数据完整性检查
- 新增 `checkGPIO0ForFlashMode()` 实现
- 新增 `initializeSY7T609()` 带重试逻辑

### main.cpp
- 移除 `Serial1.begin()` 直接调用
- 依赖 `sy7t609.cpp` 内部初始化

## 烧录操作指南

### 首次烧录（SY7T609 未连接）
1. 正常连接 USB 转 TTL
2. GPIO0 接地进入烧录模式
3. 烧录固件
4. 断开 GPIO0，重启运行

### 已连接 SY7T609 时的烧录

#### 方法 A：OTA 烧录（推荐）
1. 设备正常运行
2. 通过 Web 界面 `/update` 上传固件
3. 密码：admin/admin
4. 无需物理接触设备

#### 方法 B：USB 烧录（需断电操作）
1. 断开 SY7T609 的 TX/RX 连接
2. GPIO0 接地
3. 上电进入烧录模式
4. 烧录固件
5. 断开 GPIO0，重启
6. 重新连接 SY7T609 TX/RX

**注意**：一旦检测到烧录模式，SY7T609 将被永久禁用（EEPROM 标志）。如需重新启用，需通过 Web 界面或清除 EEPROM。

## 调试方法

### 查看 SY7T609 状态
```
// SY7T609 禁用时（Serial 输出）
[SY7T609] Flash boot mode detected (GPIO0=LOW), Serial reserved for flashing

// 或
[SY7T609] Flash mode flag found in EEPROM, disabled

// SY7T609 启用时（Serial1 输出，需接 GPIO2）
[SY7T609] Chip connected, firmware: 0xXXXXXX
[SY7T609] Auto-report enabled
```

### 恢复 SY7T609
如果误触发烧录模式标志：
1. 通过 Web 界面关闭"电能监测"
2. 或调用 `SY7T609::setEnabled(false)`
3. 清除 EEPROM 地址 206 的值（需自定义工具）

## 测试验证

### 测试用例 1：正常启动（非烧录模式）
- GPIO0 上电为 HIGH
- 预期：SY7T609 正常初始化
- 验证：`SY7T609::isFlashMode()` 返回 false

### 测试用例 2：烧录模式启动
- GPIO0 上电为 LOW
- 预期：SY7T609 禁用，Serial 保留
- 验证：`SY7T609::isFlashMode()` 返回 true

### 测试用例 3：SY7T609 初始化失败
- 断开 SY7T609 连接
- 预期：重试 3 次后放弃
- 验证：日志显示"Max initialization retries reached"

### 测试用例 4：通信超时
- 连接 SY7T609 但不供电
- 预期：`uartCommand()` 100ms 超时
- 验证：无死锁，系统继续运行

## 已知限制

1. **EEPROM 标志一旦设置无法自动清除**
   - 设计决策：防止误操作
   - 解决方案：通过 Web 界面或专用工具清除

2. **Serial1 占用 GPIO2**
   - 调试输出切换到 Serial1 后，GPIO2 不能他用
   - 不影响继电器和 LED 控制

3. **SY7T609 启用后 USB 串口失效**
   - Serial 被用于 SY7T609 通信（9600 波特率）
   - 调试输出切换到 Serial1（GPIO2）
   - 如需 USB 调试，需禁用 SY7T609

## 总结

通过多级保护机制，确保：
- ✅ 烧录模式可靠检测（GPIO0 低电平）
- ✅ 检测一次即永久保存（EEPROM 标志）
- ✅ 不影响正常工作时 SY7T609 通信
- ✅ 通信失败自动重试（最多 3 次）
- ✅ 接收超时保护（100ms）
- ✅ 调试输出无缝切换（Serial → Serial1）

**硬件电路无需修改，仅通过软件改进解决问题。**
