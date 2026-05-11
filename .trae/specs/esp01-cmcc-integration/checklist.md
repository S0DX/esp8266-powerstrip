# Checklist - ESP12S CMCC控制逻辑移植

## ⚠️ 烧录注意事项
- [x] 理解GPIO1/TX和GPIO3/RX被SY7T609占用
- [x] 知道烧录时需要断开SY7T609连接
- [x] 了解OTA烧录方案（推荐）

## 配置检查

- [x] platformio.ini 设置 board = esp12e
- [x] platformio.ini flash大小配置正确
- [x] platformio.ini 包含所有必要库

## config.h 检查

- [x] 版本号定义为 "1.0.0"
- [x] GPIO0 - 主继电器
- [x] GPIO4 - 按钮引脚
- [x] GPIO5 - 白LED
- [x] GPIO12 - 从继电器
- [x] GPIO13 - 红LED
- [x] GPIO14 - 蓝LED
- [x] GPIO15 - 继电器使能
- [x] GPIO1 - SY7T609 TX
- [x] GPIO3 - SY7T609 RX

## gpio_mgr 检查

- [x] gpio_mgr.h 定义 GPIOManager 类
- [x] 主继电器控制 (GPIO0)
- [x] 从继电器控制 (GPIO12)
- [x] 继电器使能脉冲 (GPIO15)
- [x] 蓝/红/白LED控制
- [x] LED闪烁模式

## button_mgr 检查

- [x] button_mgr.h 定义 ButtonHandler 类
- [x] 单击检测 (<300ms) - 主继电器切换
- [x] 双击检测 (<500ms) - 从继电器切换
- [x] 长按检测 (>5000ms) - 恢复出厂

## wifi_mgr 检查

- [x] wifi_mgr.h 定义 WiFiManager 类
- [x] WiFi状态机 (IDLE/CONNECTING/CONNECTED/DISCONNECTED)
- [x] EEPROM配置存储
- [x] 自动重连机制

## ota_mgr 检查

- [x] ota_mgr.h 定义 OTAManager 类
- [x] OTA初始化正确

## sy7t609 检查

- [x] sy7t609.h 定义 SY7T609 类
- [x] UART通信协议 (使用Serial1)
- [x] 数据解析正确

## energy_mgr 检查

- [x] energy_mgr.h 定义 EnergyManager 类
- [x] 能耗统计计算
- [x] EEPROM存储/恢复

## web_config 检查

- [x] Web界面完整
- [x] 实时功率图表 (canvas)
- [x] 用电统计柱状图
- [x] 主/从继电器开关
- [x] WiFi扫描和连接
- [x] OTA升级功能
- [x] 恢复出厂设置

## main.cpp 检查

- [x] 所有模块正确初始化
- [x] 主循环正确调度
- [x] 按钮回调正确连接

## 编译检查

- [x] 项目成功编译
- [x] 没有链接错误
- [x] 没有类型错误