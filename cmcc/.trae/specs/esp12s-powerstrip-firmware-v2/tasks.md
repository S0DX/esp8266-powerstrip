# Tasks

## Task 1: Web OTA 功能实现

- [x] Task 1.1: 创建 /update 端点处理固件上传
- [x] Task 1.2: 实现 Web 端 OTA 进度条显示
- [x] Task 1.3: 添加 OTA 完成/失败的状态反馈
- [x] Task 1.4: 在 Web 主页面添加固件升级入口按钮

## Task 2: WiFi AP+STA 并存模式

- [x] Task 2.1: 修改 WiFi 模式为 WIFI_AP_STA（STA+AP 混合模式）
- [x] Task 2.2: AP 改为开放网络（无密码）
- [x] Task 2.3: 实现 WiFi.scanNetworks() 扫描功能
- [x] Task 2.4: 添加 /scan 接口返回 WiFi 列表 JSON
- [x] Task 2.5: 实现 WiFi 连接稳定性处理（重连逻辑）
- [x] Task 2.6: 支持开放网络（password 为空）

## Task 3: Apple iOS 风格 UI 重构

- [x] Task 3.1: 重构 CSS 样式，采用 iOS 设计语言
- [x] Task 3.2: 添加状态卡片展示区（实时数据）
- [x] Task 3.3: 改进按钮样式（胶囊形状）
- [x] Task 3.4: 添加 iOS 风格的开关控件
- [x] Task 3.5: 添加设备信息展示区

## Task 4: 图形化用电统计

- [x] Task 4.1: 引入 Chart.js（通过 CDN）
- [x] Task 4.2: 实现实时功率曲线图（最近 30 分钟）
- [x] Task 4.3: 实现每日用电量柱状图（最近 7 天）
- [x] Task 4.4: 添加 /history 接口提供历史数据

## Task 5: 系统测试验证

- [ ] Task 5.1: 测试 Web OTA 升级流程
- [ ] Task 5.2: 测试 AP+STA 并存模式
- [ ] Task 5.3: 测试 WiFi 扫描和连接
- [ ] Task 5.4: 测试图形化展示页面

# Task Dependencies

- Task 1、2、3、4 可并行进行
- Task 5 依赖 1、2、3、4 完成后进行
