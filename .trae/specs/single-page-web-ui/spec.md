# 单页 Web UI 重构与人来上电功能完善

## Why
当前 Web 页面分为 Page1/Page2 两页，通过底部 tab-bar 切换，但 ESP8266 上单页内容量并不大，分页增加了不必要的导航复杂度和代码冗余（switchPage、滑动切换、tab-bar 等）。同时卡片排布存在冗余空行和结构不一致的问题。人来上电功能的前端流程和后端实现存在多处不完整。

## What Changes
- **BREAKING**: 移除 Page1/Page2 分页机制，所有卡片合并为单一滚动页面
- 移除 tab-bar、switchPage()、滑动切换逻辑、.page/.page.active CSS
- 移除 header 中的 pageTitle 元素
- 清理卡片间的多余空行，统一卡片间距格式
- 完善人来上电功能：前端状态显示使用正确的 JSON 字段名、后端 API 返回 wifiDetectTarget 字段

## Impact
- Affected code: `src/web_config.cpp`（HTML/CSS/JS 内嵌部分）
- 不影响后端 C++ API 路由逻辑
- 不影响其他模块

## ADDED Requirements

### Requirement: 单页滚动 UI
系统 SHALL 将所有功能卡片放在同一页面中，用户通过滚动浏览所有功能，无需切换页面。

#### Scenario: 用户打开 AP 配置页面
- **WHEN** 用户连接 AP 热点并打开 Web 页面
- **THEN** 所有功能卡片在单页中从上到下排列，可滚动浏览

### Requirement: 卡片排布整洁无冗余
系统 SHALL 保证卡片之间无多余空行，每个卡片之间仅保留一个空行分隔。

#### Scenario: 检查 HTML 源码
- **WHEN** 检查 INDEX_HTML 中的卡片结构
- **THEN** 相邻 `</div>` 和 `<div class="card">` 之间恰好一个空行，无连续空行

### Requirement: 人来上电功能完整
系统 SHALL 提供完整的人来上电功能流程：启用 → 选择目标WiFi → 设置RSSI阈值 → 联锁倒计时 → 状态显示。

#### Scenario: 人来上电完整流程
- **WHEN** 用户启用"人来上电"
- **THEN** 自动启用拔除断电和联锁倒计时
- **WHEN** 用户选择目标 WiFi
- **THEN** 前端显示目标名称，后端保存目标 SSID
- **WHEN** 后端扫描到目标 WiFi 出现
- **THEN** 自动开启主继电器和从继电器
- **WHEN** 目标 WiFi 消失且联锁倒计时启用
- **THEN** 启动倒计时关闭继电器
- **WHEN** 前端轮询 /api/status
- **THEN** 正确显示 wifiDetectTarget（当前使用 d.target 是错误的，应为 d.wifiDetectTarget）

## MODIFIED Requirements

### Requirement: 人来上电状态显示
前端 update() 函数中，WiFi 检测状态显示 SHALL 使用 `d.wifiDetectTarget` 而非 `d.target`，因为后端 /api/status 返回的 JSON 字段名为 `wifiDetectTarget`。

## REMOVED Requirements

### Requirement: 双页切换导航
**Reason**: 所有功能合并为单页，不再需要分页导航
**Migration**: 移除 tab-bar HTML、switchPage() JS、滑动切换事件监听、.page/.page.active CSS、header pageTitle 元素
