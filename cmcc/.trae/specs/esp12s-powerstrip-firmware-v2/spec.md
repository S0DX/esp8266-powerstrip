# ESP12S 插排固件改进规格说明书

## Why

当前固件存在以下问题需要修复和增强：
1. Web OTA 功能未实现（需要 port 80 HTTP 方式）
2. AP 和 WiFi 不能同时工作，WiFi 扫描功能缺失
3. Web 界面风格简陋，不够美观
4. 缺少图形化的用电统计展示

## What Changes

- **OTA 功能增强**：实现基于 HTTP 的 Web OTA（port 80），与 Arduino OTA（port 8266）并存
- **WiFi 逻辑修复**：AP 始终开启，WiFi 可扫描、可连接，支持开放网络
- **UI 风格改进**：采用 Apple iOS 风格设计，简洁大气
- **图形化展示**：用电数据以 Chart.js 图表展示

## Impact

- Affected code：`web_config.cpp/h`, `wifi_mgr.cpp/h`, `main.cpp`
- 新增依赖：Chart.js（通过 CDN 引入）

## ADDED Requirements

### Requirement: Web OTA 功能（port 80）

系统 SHALL 提供基于 HTTP 的 Web OTA 升级功能，用户可通过浏览器上传固件文件进行升级。

#### Scenario: 通过浏览器升级固件
- **WHEN** 用户访问设备 IP:80 并选择 .bin 文件上传
- **THEN** 系统接收文件并执行 OTA 升级，升级过程中显示进度条
- **AND** 升级完成后自动重启

### Requirement: AP + STA 并存模式

系统 SHALL 在已连接 WiFi 的同时保持 AP 开启，允许设备在配网模式和正常模式间切换。

#### Scenario: AP 始终开启
- **WHEN** 设备启动且有保存的 WiFi 配置
- **THEN** WiFi 工作在 STA+AP 混合模式（WiFi.mode(WIFI_AP_STA)）
- **AND** AP 始终广播，可随时进入配置页面

#### Scenario: WiFi 扫描与选择
- **WHEN** 用户进入配置页面
- **THEN** 自动扫描周围 WiFi 热点并以列表形式展示信号强度
- **AND** 用户可选择网络并输入密码连接
- **AND** 支持开放网络（无密码）

#### Scenario: WiFi 连接稳定性处理
- **WHEN** WiFi 连接失败（超时或密码错误）
- **THEN** 保持在 AP 模式，允许用户重新选择网络
- **AND** 不自动重启或进入死循环
- **WHEN** WiFi 信号丢失
- **THEN** 设备尝试自动重连（最多 3 次，间隔 10 秒）
- **AND** 3 次失败后保持 AP 模式运行

### Requirement: Apple iOS 风格 UI

系统 SHALL 提供简洁美观的 Apple iOS 风格 Web 配置界面。

#### Scenario: 界面风格
- **WHEN** 用户打开配置页面
- **THEN** 界面采用 iOS 设计语言：
  - 白色卡片式布局，圆角 12px
  - 字体使用 San Francisco 或系统默认
  - 配色以白色+灰色为主，强调色蓝色 #007AFF
  - 按钮使用大圆角胶囊样式
  - 状态指示使用圆形图标

### Requirement: 图形化用电统计

系统 SHALL 在 Web 页面中以图表形式展示用电数据。

#### Scenario: 实时功率曲线
- **WHEN** 用户访问设备主页
- **THEN** 显示最近 30 分钟的实时功率曲线图
- **AND** 电压、电流、功率因数等数据以卡片形式展示

#### Scenario: 每日用电量柱状图
- **WHEN** 用户查看用电历史
- **THEN** 显示最近 7 天的每日用电量柱状图

## MODIFIED Requirements

### Requirement: WiFi 连接管理

**MODIFIED from**：原有 WiFi 逻辑在配置模式下完全切换到 AP，连接 WiFi 后切换到 STA

**MODIFIED to**：
- 上电默认启用 STA+AP 混合模式
- AP 不再需要密码（开放网络）
- 保存多个 WiFi 配置（最多 3 个）
- 支持 WiFi 扫描和选择

### Requirement: OTA 升级架构

**MODIFIED from**：只有 Arduino OTA（port 8266）

**MODIFIED to**：
- 保留 Arduino OTA（port 8266）用于开发者工具升级
- 新增 Web OTA（port 80）用于普通用户浏览器升级
- 两种 OTA 方式可并存

## REMOVED Requirements

### Requirement: 独立配网模式

**Reason**：AP 始终开启后，不再需要单独的配网模式切换
**Migration**：配置页面始终可访问，AP 开放网络无密码
