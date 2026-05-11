# 添加更多设置二级页面

## Why
当前所有设置卡片都在同一页面，随着功能增多页面过长。需要将高级设置（倒计时、循环、MQTT、拔除断电、计费供电、电费等）移到二级页面，主页面只保留核心功能（继电器控制、人来上电、实时电量、用电历史、WiFi 状态、系统设置、设备信息）。

## What Changes
- 在"系统设置"卡片中添加"更多设置"按钮
- 添加二级页面模态框，包含设备信息之后的所有卡片
- 版本号从当前版本更新到 2.0

## Impact
- Affected code: `src/web_config.cpp`（HTML/CSS/JS 内嵌部分）
- 不影响后端 API 逻辑

## ADDED Requirements

### Requirement: 更多设置按钮
系统 SHALL 在"系统设置"卡片中添加"更多设置"按钮，点击后打开二级页面模态框。

#### Scenario: 用户点击更多设置
- **WHEN** 用户点击"更多设置"按钮
- **THEN** 打开全屏模态框，显示高级设置卡片
- **THEN** 模态框包含关闭按钮

### Requirement: 二级页面内容
二级页面 SHALL 包含以下卡片（按顺序）：
1. 倒计时关闭
2. 24 小时时间段循环
3. MQTT 代理平台
4. 设备拔除断电设置
5. 计费供电
6. 电费计算

### Requirement: 版本号更新
设备信息中的版本号 SHALL 更新为 "2.0"。

## MODIFIED Requirements

### Requirement: 系统设置卡片
在 AP 热点密码输入框下方添加"更多设置"按钮。

## REMOVED Requirements
无
