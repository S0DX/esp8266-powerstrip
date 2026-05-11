# 功率阈值默认值初始化修复

## Why
当前代码在 EEPROM 数据无效时使用默认值 0.5W，但**不写入 EEPROM**。导致：
1. 首次使用或恢复出厂后，EEPROM 为 255（无效值），显示 0.5W
2. 用户设置过一次 1.04W 后，EEPROM 永久存储 1.04W
3. 用户无法恢复到 0.5W 默认值（除非手动保存）

应该在检测到无效值时，不仅使用默认值，还要**写入 EEPROM**，确保默认值持久化。

## What Changes
- 修改 `GPIOManager::init()` 中功率阈值的加载逻辑
- 当 EEPROM 值无效时，设置为 0.5f 并**写入 EEPROM**
- 对计费供电阈值也应用相同逻辑

## Impact
- Affected code: `src/gpio_mgr.cpp`
- 不影响已正常使用的用户
- 仅影响首次使用或 EEPROM 数据无效的情况

## ADDED Requirements

### Requirement: 默认值持久化
系统 SHALL 在检测到 EEPROM 数据无效时，使用默认值并写入 EEPROM。

#### Scenario: 首次使用或数据无效
- **WHEN** EEPROM 中功率阈值值为 0 或 >200（无效范围）
- **THEN** 使用默认值 0.5W 并写入 EEPROM
- **THEN** 下次启动时读取到有效值 0.5W

## MODIFIED Requirements

### Requirement: 功率阈值加载逻辑
加载功率阈值时， SHALL 检查 EEPROM 值的有效性，无效则使用默认值并写入。

## REMOVED Requirements
无
