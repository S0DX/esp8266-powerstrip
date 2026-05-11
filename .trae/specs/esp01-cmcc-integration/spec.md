# ESP12S智能插排 - CMCC控制逻辑移植规格

## Why
需要将cmcc智能插排项目的完整模块化架构和控制逻辑移植到ESP12S平台，实现双继电器控制、电能监测、物理按钮和LED状态指示功能。

## What Changes

### 架构变化
- 采用模块化设计：gpio_mgr、button_mgr、wifi_mgr、energy_mgr、ota_mgr分离
- 完整保留CMCC项目的所有功能
- 增强错误处理和状态管理
- 改进LED状态指示系统

### GPIO分配 (ESP12S)
- **GPIO0** - 主继电器 (RELAY_MASTER)
- **GPIO4** - 按钮输入 (BUTTON)
- **GPIO5** - 白LED (LED_WHITE)
- **GPIO12** - 从继电器 (RELAY_SLAVE)
- **GPIO13** - 红LED (LED_RED)
- **GPIO14** - 蓝LED (LED_BLUE)
- **GPIO15** - 继电器使能 (RELAY_ENABLE)
- **GPIO16** - 无连接（ESP8266特定）
- **GPIO1 (TX)** - SY7T609 UART TX
- **GPIO3 (RX)** - SY7T609 UART RX

### ⚠️ 烧录注意事项
**GPIO1和GPIO3被SY7T609占用，串口烧录时需要：**
- 烧录前断开SY7T609模块的TX/RX连接
- 或使用OTA方式烧录（推荐，烧录一次后使用OTA升级）

### 完整功能
- 双继电器控制（主/从）
- SY7T609电能监测芯片（电压、电流、功率、功率因数、频率、温度、能耗）
- 物理按钮控制（单击切换主继电器，双击切换从继电器，长按恢复出厂）
- 三色LED状态指示（蓝/红/白）
- WiFi AP/STA模式
- MQTT支持（可选保留）
- OTA升级
- 电能统计图表
- Web配置界面

## Impact

### 受影响的规格
- 继电器控制逻辑（双继电器）
- 电能监测和统计
- Web界面（功率图表、用电统计）

### 受影响代码
- src/main.cpp - 重组为模块化结构
- src/gpio_mgr.cpp/h - GPIO和LED管理
- src/button_mgr.cpp/h - 按钮处理
- src/wifi_mgr.cpp/h - WiFi管理
- src/energy_mgr.cpp/h - 电能统计
- src/ota_mgr.cpp/h - OTA管理
- src/sy7t609.cpp/h - 电能监测芯片驱动
- src/web_config.cpp/h - Web服务器

## ADDED Requirements

### Requirement: 双继电器控制
系统应控制主继电器和从继电器

#### Scenario: 主继电器控制
- **WHEN** 主继电器需要开启
- **THEN** GPIO0输出LOW，触发RELAY_ENABLE

#### Scenario: 从继电器控制
- **WHEN** 从继电器需要开启
- **THEN** GPIO12输出LOW，触发RELAY_ENABLE

### Requirement: SY7T609电能监测
系统应读取并显示电能参数

#### Scenario: 电能数据
- **WHEN** 系统运行
- **THEN** 实时显示电压、电流、功率、功率因数、频率、温度、能耗

### Requirement: 物理按钮控制
系统应支持按钮操作

#### Scenario: 单击按钮
- **WHEN** 短按按钮（<300ms）
- **THEN** 主继电器状态翻转

#### Scenario: 双击按钮
- **WHEN** 双击按钮（500ms内）
- **THEN** 从继电器状态翻转

#### Scenario: 长按按钮 (>5秒)
- **WHEN** 按住按钮超过5秒
- **THEN** 恢复出厂设置，设备重启

### Requirement: LED状态指示
系统应通过三色LED显示设备状态

#### Scenario: WiFi连接中
- **WHEN** WiFi正在连接
- **THEN** 蓝灯闪烁（500ms间隔）

#### Scenario: WiFi已连接
- **WHEN** WiFi连接成功
- **THEN** 蓝LED常亮

#### Scenario: WiFi断开/错误
- **WHEN** WiFi断开或出错
- **THEN** 红灯闪烁（500ms间隔）

#### Scenario: 继电器开启
- **WHEN** 从继电器开启
- **THEN** 白灯点亮

## MODIFIED Requirements

### Requirement: Web界面
完整保留CMCC项目的Web界面，包含功率实时图表、用电柱状图、继电器开关、WiFi配置

### Requirement: WiFi管理
增强WiFi连接状态机，支持自动重连机制