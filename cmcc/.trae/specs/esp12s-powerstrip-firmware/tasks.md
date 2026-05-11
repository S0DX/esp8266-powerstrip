# Tasks

## Task 1: ESP12S IO 口功能分析

- [x] Task 1.1: 整理 powerboard-test.yaml 中所有 GPIO 定义，建立 GPIO 功能表
- [x] Task 1.2: 分析各 GPIO 的电气特性（上下拉、 inverted、驱动方式）
- [x] Task 1.3: 明确 UART（TX=GPIO1/RX=GPIO3）与 SY7T609 的连接关系
- [x] Task 1.4: 整理继电器触发时序（GPIO15 ENABLE 脉冲机制）
- [x] Task 1.5: 输出完整的 IO 口功能分析文档

## Task 2: 建立固件项目框架

- [x] Task 2.1: 创建 ESP8266 Arduino 项目结构（src/main.cpp、src/wifi_manager.cpp、src/ota_manager.cpp 等）
- [x] Task 2.2: 配置 platformio.ini 或 Arduino IDE 项目参数（board=esp12e，flash=4MB）
- [x] Task 2.3: 实现 app_main() 入口，初始化串口日志
- [x] Task 2.4: 配置 WiFi 连接（从 Flash 读取保存的 SSID/Password，支持配网模式）
- [x] Task 2.5: 注册 Arduino OTA 服务，实现基本 OTA 升级框架

## Task 3: 按键处理模块

- [x] Task 3.1: 实现 GPIO4 按键去抖动读取（使用中断或轮询）
- [x] Task 3.2: 实现单击检测（≤300ms）——切换主继电器
- [x] Task 3.3: 实现双击检测（≤500ms间隔）——切换从继电器
- [x] Task 3.4: 实现长按检测（≥5s）——恢复出厂设置+进入配网模式
- [ ] Task 3.5: 测试验证各按键场景

## Task 4: 继电器控制模块

- [x] Task 4.1: 实现 GPIO0（主继电器）和 GPIO12（从继电器）GPIO 输出控制
- [x] Task 4.2: 实现 GPIO15 ENABLE 脉冲触发逻辑（10ms 脉冲）
- [x] Task 4.3: 实现主从继电器互锁逻辑（从继电器依赖主继电器）
- [x] Task 4.4: 实现继电器状态保存与恢复（断电记忆）
- [ ] Task 4.5: 测试验证继电器响应时序

## Task 5: LED 状态指示模块

- [x] Task 5.1: 实现 GPIO16（蓝灯）、GPIO14（红灯）、GPIO5（白灯）PWM 输出控制
- [x] Task 5.2: 实现 LED 状态机（WiFi 连接状态指示、配网模式提示）
- [x] Task 5.3: 实现白灯跟随从继电器状态（通电亮灯）

## Task 6: SY7T609 电能计量驱动移植

- [x] Task 6.1: 从 esphome_custom_components 提取 sy7t609 UART 通信协议代码
- [x] Task 6.2: 适配到 ESP8266 Arduino 环境（SoftwareSerial 或 HardwareSerial）
- [x] Task 6.3: 实现电能数据读取（电压/电流/功率/功率因数/频率/温度/能耗）
- [x] Task 6.4: 实现计量芯片校准功能（电压校准、电流增益调整）
- [x] Task 6.5: 实现电量掉电记忆（累计电量写入 Flash）
- [ ] Task 6.6: 测试验证计量数据准确性

## Task 7: OTA 升级功能完善

- [x] Task 7.1: 实现 OTA 升级进度回调（打印升级百分比）
- [x] Task 7.2: 实现 OTA 升级完成自动重启
- [x] Task 7.3: 实现 OTA 升级失败检测与回滚
- [ ] Task 7.4: 验证 OTA 固件升级流程（从 v1.0 升级到 v2.0）

## Task 8: 系统集成与测试

- [ ] Task 8.1: 集成所有模块，系统联调测试
- [ ] Task 8.2: 验证各场景：按键控制、远程控制、计量数据、OTA 升级
- [ ] Task 8.3: 功耗测试与稳定性验证

# Task Dependencies

- Task 3（按键）依赖 Task 2（固件框架）
- Task 4（继电器）依赖 Task 2（固件框架）
- Task 5（LED）依赖 Task 2（固件框架）
- Task 6（计量驱动）依赖 Task 2（固件框架）
- Task 7（OTA）依赖 Task 2（固件框架）
- Task 8（系统集成）依赖 Task 3、4、5、6、7 全部完成
- Task 1（IO 分析）和 Task 2 可以并行进行
