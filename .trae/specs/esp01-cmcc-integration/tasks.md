# Tasks - ESP12S CMCC控制逻辑移植

## 任务列表

- [ ] Task 1: 创建 platformio.ini 配置 (ESP12S)
  - [ ] 设置 board = esp12e
  - [ ] 配置适当flash大小
  - [ ] 添加必要库依赖

- [ ] Task 2: 创建 config.h 配置文件
  - [ ] 定义版本号
  - [ ] 定义所有GPIO引脚
  - [ ] 定义按钮参数
  - [ ] 定义EEPROM地址

- [ ] Task 3: 创建 gpio_mgr 模块
  - [ ] 创建 gpio_mgr.h 头文件
  - [ ] 创建 gpio_mgr.cpp 实现文件
  - [ ] 实现主/从继电器控制
  - [ ] 实现三色LED管理
  - [ ] 实现继电器使能脉冲

- [ ] Task 4: 创建 button_mgr 模块
  - [ ] 创建 button_mgr.h 头文件
  - [ ] 创建 button_mgr.cpp 实现文件
  - [ ] 实现单击检测（主继电器切换）
  - [ ] 实现双击检测（从继电器切换）
  - [ ] 实现长按检测（>5秒恢复出厂）

- [ ] Task 5: 创建 wifi_mgr 模块
  - [ ] 创建 wifi_mgr.h 头文件
  - [ ] 创建 wifi_mgr.cpp 实现文件
  - [ ] 实现WiFi状态机
  - [ ] 实现EEPROM配置存储/加载

- [ ] Task 6: 创建 ota_mgr 模块
  - [ ] 创建 ota_mgr.h 头文件
  - [ ] 创建 ota_mgr.cpp 实现文件

- [ ] Task 7: 创建 sy7t609 模块（电能监测）
  - [ ] 创建 sy7t609.h 头文件
  - [ ] 创建 sy7t609.cpp 实现文件
  - [ ] 实现UART通信协议

- [ ] Task 8: 创建 energy_mgr 模块
  - [ ] 创建 energy_mgr.h 头文件
  - [ ] 创建 energy_mgr.cpp 实现文件
  - [ ] 实现能耗统计和存储

- [ ] Task 9: 创建 web_config 模块
  - [ ] 创建 web_config.h 头文件
  - [ ] 创建 web_config.cpp 实现文件
  - [ ] 实现Web界面（功率图表、用电统计）
  - [ ] 实现继电器开关UI
  - [ ] 实现WiFi配置UI

- [ ] Task 10: 创建 main.cpp 主程序
  - [ ] 整合所有模块
  - [ ] 实现主循环调度

- [ ] Task 11: 编译验证
  - [ ] 编译项目
  - [ ] 修复任何编译错误

## 任务依赖

- Task 1-9 可并行执行（模块独立开发）
- Task 10 依赖 Task 1-9
- Task 11 依赖 Task 10