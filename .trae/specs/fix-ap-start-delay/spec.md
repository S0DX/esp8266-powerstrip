# AP 启动延迟修复

## Why
用户反馈启动 AP 热点有时存在延迟，导致连接热点后 Web 页面响应慢。当前代码在 AP 启动后仅 `yield()` 一次，立即开始 STA 连接，若连接失败会阻塞 AP 响应。

## What Changes
- 重构 `WiFiManager::init()`，将 AP 启动和 STA 连接分离
- AP 启动后增加更多 `yield()` 调用，确保 DNS 和 Web 服务器能正常响应
- STA 连接改为非阻塞方式，在 `handle()` 中逐步进行

## Impact
- Affected code: `src/wifi_mgr.cpp`
- 不影响 API 和行为，仅优化启动顺序

## ADDED Requirements

### Requirement: AP 优先启动
系统 SHALL 在启动时优先确保 AP 热点完全就绪，然后再处理 STA 连接。

#### Scenario: 设备上电启动
- **WHEN** 设备启动
- **THEN** AP 热点在 1 秒内完全就绪，可响应客户端请求
- **THEN** STA 连接在后台非阻塞进行

### Requirement: 非阻塞 STA 连接
系统 SHALL 将 STA 连接过程分散到多次 `handle()` 调用中，避免阻塞 AP 响应。

#### Scenario: 连接已保存的 WiFi
- **WHEN** 有已保存的 WiFi 配置
- **THEN** `init()` 中仅设置连接标志，不立即调用 `WiFi.begin()`
- **THEN** 在 `handle()` 中检查标志并启动连接，每次调用都 `yield()`

## MODIFIED Requirements

### Requirement: WiFiManager::init() 重构
`init()` 函数 SHALL 只负责启动 AP 热点，STA 连接延迟到 `handle()` 中进行。

## REMOVED Requirements
无
