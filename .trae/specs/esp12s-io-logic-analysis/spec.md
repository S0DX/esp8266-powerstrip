# ESP12S 智能插排 - IO 口设置与程序逻辑分析

## Why
完整梳理 ESP12S 智能插排项目的 IO 口分配、硬件连接关系和程序逻辑架构，为 debug 提供清晰的参考文档。

## IO 口分配总览

### GPIO 引脚定义（config.h）
```
GPIO0  - RELAY_MASTER_PIN    - 主继电器控制（低电平触发）
GPIO4  - BUTTON_PIN          - 物理按钮（上拉输入）
GPIO5  - LED_WHITE_PIN       - 白 LED（分控继电器指示灯，低电平点亮）
GPIO12 - RELAY_SLAVE_PIN     - 从继电器控制（低电平触发）
GPIO14 - LED_RED_PIN         - 红 LED（WiFi 错误指示，低电平点亮）
GPIO15 - RELAY_ENABLE_PIN    - 继电器使能引脚（共享使能，低电平触发）
GPIO16 - LED_BLUE_PIN        - 蓝 LED（WiFi 状态指示，低电平点亮）
GPIO1  - UART_TX_PIN         - SY7T609 UART TX（9600 波特率）
GPIO3  - UART_RX_PIN         - SY7T609 UART RX
```

### 电气特性
- **继电器控制逻辑**：
  - 所有继电器引脚默认输出 HIGH（继电器关闭）
  - 开启时输出 LOW，并触发使能引脚 10ms 脉冲
  - 主继电器关闭时自动关闭从继电器
  
- **LED 控制逻辑**：
  - 所有 LED 低电平点亮（active LOW）
  - 支持常亮、闪烁、关闭三种模式
  - 闪烁频率 500ms 间隔

- **按钮输入**：
  - 内部上拉，按下为 LOW
  - 消抖时间 50ms
  - 单击 <300ms，双击 <500ms，长按 >10000ms

## 程序逻辑架构

### 1. 系统初始化流程（setup()）
```
1. Serial 初始化 (115200)
2. GPIOManager::init()
   - 初始化继电器引脚（OUTPUT, HIGH）
   - 初始化 LED 引脚（OUTPUT, HIGH）
   - 初始化按钮引脚（INPUT_PULLUP）
   - 读取 EEPROM 锁定状态
3. ButtonHandler::init()
4. WiFiManager::init()
   - AP+STA 模式
   - 加载保存的 WiFi 配置
5. OTAManager::init()
6. WebConfigServer::init()
7. SY7T609::init()
   - 检测 GPIO0 电平（烧录模式检测）
   - 初始化 UART 通信
8. EnergyManager::init()
```

### 2. 主循环（loop()）
```
每帧执行:
- WiFiManager::handle()        - WiFi 状态机
- OTAManager::handle()         - OTA 处理
- WebConfigServer::handle()    - Web 服务器
- ButtonHandler::handle()      - 按钮检测
- GPIOManager::updateLEDs()    - LED 闪烁更新
- SY7T609::handle()            - 电能数据读取
- EnergyManager::handle()      - 能耗统计
- delay(1)

每 5 秒输出调试信息（通过 Serial 或 Serial1）
```

### 3. 关键模块逻辑

#### 3.1 GPIO 管理（gpio_mgr.cpp）
- **继电器控制**：
  - `setRelayMaster(bool on)`: 控制主继电器，关闭时自动关闭从继电器
  - `setRelaySlave(bool on)`: 控制从继电器，开启时自动开启主继电器
  - `triggerRelayEnable()`: 产生 10ms 使能脉冲
  
- **LED 控制**：
  - `setBlueMode()`: 设置蓝灯模式（常亮/闪烁/关闭）
  - `setRedMode()`: 设置红灯模式
  - `updateLEDs()`: 每帧调用，处理闪烁逻辑
  
- **锁定模式**：
  - 锁定时禁用物理按钮，仅 Web 控制
  - 锁定状态保存在 EEPROM 地址 200

#### 3.2 按钮处理（button_mgr.cpp）
- **状态机**：
  - 检测按下/释放事件
  - 消抖处理
  - 单击/双击/长按识别
  
- **回调逻辑**（main.cpp line 87-114）：
  - 单击：切换主继电器
  - 双击：切换从继电器
  - 长按：恢复出厂设置并重启

#### 3.3 WiFi 管理（wifi_mgr.cpp）
- **状态机**：
  - STATE_IDLE: 空闲
  - STATE_CONNECTING: 连接中（超时 8 秒）
  - STATE_CONNECTED: 已连接
  - STATE_DISCONNECTED: 断开（5 秒后重试）
  
- **重连机制**：
  - 最多重试 3 次
  - 失败后进入 IDLE 状态
  - 红灯闪烁指示错误

#### 3.4 SY7T609 电能监测（sy7t609.cpp）
- **初始化检测**：
  - 检测 GPIO0 是否为 LOW（烧录模式）
  - 烧录模式下禁用 SY7T609，保留 Serial 用于烧录
  
- **通信协议**：
  - Modbus RTU over UART (9600 8N1)
  - 7 字节命令/响应帧
  - 支持自动上报和寄存器读取两种模式
  
- **数据读取**：
  - 自动上报模式：解析 0x55AA 帧头
  - 寄存器模式：读取 VRMS、IRMS、POWER、PF、FREQUENCY、TEMPERATURE
  - 读取间隔 1 秒
  
- **调试输出切换**：
  - SY7T609 启用时，DBG_PRINTF 输出到 Serial1（GPIO2）
  - SY7T609 禁用时，DBG_PRINTF 输出到 Serial（GPIO1）

#### 3.5 能源管理（energy_mgr.cpp）
- **能耗累计**：
  - 基于 SY7T609 的累计能量值
  - 增量计算防止重复统计
  
- **EEPROM 保存**：
  - 每 30 秒保存一次
  - 地址 128-135（4 字节魔数 +4 字节浮点）
  - 魔数 0xDEADBEEF

#### 3.6 Web 服务器（web_config.cpp）
- **API 端点**：
  - `/api/status`: 获取设备状态
  - `/api/relay?w=m|s`: 控制继电器
  - `/api/lock`: 切换锁定模式
  - `/api/meter?a=on|off`: 启用电能监测
  - `/api/scan`: 扫描 WiFi
  - `/api/connect`: 连接 WiFi
  - `/api/reset`: 恢复出厂
  - `/update`: OTA 升级（密码 admin/admin）

### 4. EEPROM 布局
```
地址 0-63:   WiFi SSID (32 字节)
地址 64-127: WiFi 密码 (64 字节)
地址 128-135: 能耗数据 (4 字节魔数 +4 字节 float)
地址 200:    GPIO 锁定标志 (0x42=锁定)
地址 205:    SY7T609 使能标志 (1=启用)
地址 254:    WiFi 配置魔数 (0xAB)
```

## 调试关键点

### 1. 继电器不动作
- 检查 GPIO0/GPIO12 输出电平
- 检查 GPIO15 使能脉冲
- 确认继电器驱动电路

### 2. 按钮无响应
- 检查 GPIO4 输入电平
- 确认锁定模式是否启用
- 检查消抖时间设置

### 3. LED 不亮
- 确认低电平驱动逻辑
- 检查 GPIO5/14/16 输出
- 确认 updateLEDs() 调用频率

### 4. WiFi 连接失败
- 检查天线连接
- 确认 WiFi 配置正确
- 查看状态机日志

### 5. SY7T609 无数据
- 检查 UART 接线（TX/RX 交叉）
- 确认波特率 9600
- 检查芯片供电
- 确认非烧录模式（GPIO0 高电平）

## 已知设计问题

### 1. GPIO0 冲突
- GPIO0 同时用作主继电器控制和 Flash Boot 检测
- 烧录时需要 GPIO0 为 LOW，但正常工作时为 OUTPUT HIGH
- 解决方案：SY7T609::init() 中临时切换 GPIO0 为 INPUT_PULLUP 检测电平

### 2. UART 冲突
- GPIO1/3 用于 SY7T609 通信，与 USB 串口共用
- SY7T609 启用时，调试输出切换到 Serial1（GPIO2）
- 烧录时需断开 SY7T609 或使用 OTA

### 3. 继电器互锁
- 从继电器开启时自动开启主继电器
- 主继电器关闭时自动关闭从继电器
- 可能导致意外的电源切断
