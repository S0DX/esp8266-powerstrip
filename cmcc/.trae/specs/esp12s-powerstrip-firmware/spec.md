# ESP12S 插排固件开发规格说明书

## Why

当前 `C:\Users\ASUS\Desktop\cmcc` 目录下存在一套围绕 ESP12S 主控的智能插排散料信息，包含原理图 SVG、ESPHome 配置、SY7T609 电能计量芯片驱动代码等，但缺乏系统性的 IO 口功能分析文档，不利于后续构建可 OTA 升级的插排控制固件。

## What Changes

本规格书将完成以下工作：

- **IO 口功能分析**：基于 `powerboard-test.yaml` 中的 GPIO 定义和原理图，明确 ESP12S 各 IO 口在插排中的实际作用与使用方法
- **现有代码梳理**：梳理 esphome_custom_components（SY7T609 电能计量驱动）、Arduino 原生驱动代码
- **OTA 固件架构设计**：设计基于 ESP8266 RTOS SDK 或 ESP8266 Arduino Core 的可 OTA 插排控制固件框架
- **固件实现**：实现包括按键控制、继电器控制、LED 指示、UART 计量芯片通信、OTA 升级等核心功能

## Impact

- Affected specs：智能插排控制、能耗监测、远程 OTA 升级
- Affected code：
  - `C:\Users\ASUS\Desktop\cmcc\powerboard-test.yaml`（GPIO 定义参考）
  - `C:\Users\ASUS\Desktop\cmcc\esphome_custom_components-main\components\sy7t609\`（SY7T609 ESPHome 驱动）
  - `C:\Users\ASUS\Desktop\cmcc\SY7T609\`（SY7T609 Arduino 原生驱动）
  - `C:\Users\ASUS\Desktop\cmcc\1-common-esp8266-4m-lite.yaml`（OTA 配置参考）

## ADDED Requirements

### Requirement: ESP12S IO 口功能分析文档

系统 SHALL 提供 ESP12S 插排中各 GPIO 引脚的功能说明、使用注意事项及复用考虑。

#### Scenario: IO 口识别
- **GIVEN** 插排原理图和 `powerboard-test.yaml` GPIO 定义
- **WHEN** 分析各 GPIO 的实际连接
- **THEN** 输出完整的 GPIO 功能表（引脚号、功能、方向、驱动方式、注意事项）

### Requirement: 可 OTA 的固件框架

固件 SHALL 基于 ESP8266 Arduino Core 实现模块化架构，支持通过 WiFi OTA 升级。

#### Scenario: 基础框架搭建
- **WHEN** 建立固件项目结构
- **THEN** 实现：app_main 入口、WiFi 连接管理、OTA 服务注册、LED 状态指示、系统重启处理

#### Scenario: 按键处理
- **WHEN** 用户操作插排物理按键
- **THEN** 实现：单击切换主继电器、双击切换从继电器、长按 5 秒触发恢复出厂设置并重新配网

#### Scenario: 继电器控制
- **WHEN** 控制继电器通断
- **THEN** 实现：主继电器（GPIO0）和从继电器（GPIO12）独立控制，通过 GPIO15（ENABLE）触发脉冲控制

#### Scenario: LED 状态指示
- **WHEN** 系统运行状态变化
- **THEN** 实现：蓝灯常亮=WiFi 已连接、蓝灯闪烁=配网模式、红灯闪烁+蓝灯灭=WiFi 未连接、白灯=从继电器通电状态

#### Scenario: SY7T609 电能计量
- **WHEN** 通过 UART 与 SY7T609 芯片通信
- **THEN** 实现：读取电压、电流、功率、功率因数、频率、温度、能耗数据，支持校准功能

#### Scenario: OTA 固件升级
- **WHEN** 发布新版本固件
- **THEN** 实现：WiFi OTA 升级流程，包含固件版本校验、升级进度反馈、升级失败回滚机制

### Requirement: 继电器互锁逻辑

系统 SHALL 确保主从继电器逻辑安全，防止异常状态。

#### Scenario: 从继电器依赖主继电器
- **WHEN** 用户尝试开启从继电器
- **THEN** 系统自动确保主继电器处于开启状态；从继电器开启时若主继电器断开，白灯熄灭提示用户

### Requirement: 电量掉电记忆

系统 SHALL 在断电后记忆累计用电量。

#### Scenario: 电量持久化
- **WHEN** 设备断电
- **THEN** 将累计电量写入 ESP8266 Flash，重新上电后读取并累加

## MODIFIED Requirements

### Requirement: SY7T609 计量芯片驱动适配

**MODIFIED from**：原有 esphome_custom_components 中的 sy7t609 驱动面向 ESPHome 框架，本次需将其核心 UART 通信逻辑移植到独立固件中使用。

## REMOVED Requirements

### Requirement: 原 esphome_custom_components 框架依赖

**Reason**：最终固件为原生 Arduino/ESP8266 SDK 实现，不依赖 ESPHome 框架
**Migration**：提取 sy7t609 UART 通信协议代码，独立集成到固件中
