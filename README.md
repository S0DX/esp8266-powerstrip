# 智能插排固件 (Smart Power Strip Firmware) v5.0

基于 ESP8266 (ESP-12E/F) 的智能插排固件，支持远程控制、电能监测、定时/倒计时功能、mDNS 局域网域名、UDP 设备发现、Captive Portal 跨平台门户弹出。

## 功能特性

### 继电器控制
- **主继电器（总控）**：控制插座主体电源
- **从继电器（分控）**：独立控制的输出端
- **批量控制**：支持同时开关双继电器
- **锁定模式**：禁用物理按键，仅 Web 端可控

### 电能监测 (SY7T609)
- 实时电压、电流、功率、功率因数显示
- 累计用电量统计（kWh）
- 月度/上月用电对比
- 7天用电历史记录
- **状态机驱动非阻塞读取**（v5.0）：主循环零阻塞，单项故障不阻塞整体轮询
- 电压/电流自动校准、恢复出厂校准、清零电能

### 智能功能
- **倒计时关闭**：选择时长后自动关闭继电器
- **24小时循环**：最多3个时段，自动保持开启
- **功耗优化（含拔除断电保护）**：功率低于阈值时自动关闭（v5.0 合并卡片）
- **计费供电**：支持按电量/金额/时长阈值关闭，内置电价设置（v5.0 迁入）
- **人来上电**：检测指定WiFi信号自动开启，支持：
  - 信号消失联锁倒计时
  - 仅开主继电器模式
  - 按键检测（锁定模式下物理按键触发单次扫描）
- **MQTT 集成**：支持接入 Home Assistant 等智能家居平台

### 网络功能
- **mDNS 域名**：默认 `power.local`，支持自定义
  - v5.0 可靠性提升：`MDNS.begin()` 失败重试 3 次，`MDNS.update()` 独立于 STA 状态调用，Web 请求间隙兜底响应
- **Captive Portal**：跨平台门户弹出（v5.0 新增 Android `/generate_204`、Windows `/connecttest.txt` 探测端点）
- **UDP 设备发现**：广播设备信息，便于 App 发现
- **AP + STA 双模式**：配置时不断开连接
- **Web OTA 升级**：支持网页上传固件升级
- **加载动画超时保护**（v5.0）：fetch 5 秒超时 + 3 秒保底重试，避免页面卡在加载状态

### 其他
- WiFi 断线红灯告警（可关闭）
- 卡片排序和显示控制
- 物理按键单击/双击/长按支持

## 硬件要求

### 推荐硬件
- ESP-12E 或 ESP-12F 模块
- SY7T609 电能监测芯片
- 双路锁存继电器模块（总控+分控）
- 蓝色、红色、白色 LED 各一个
- 物理按键一个

### GPIO 分配

| GPIO | 功能 | 说明 |
|------|------|------|
| 0 | RELAY_MASTER_PIN | 总控继电器（兼 Flash Boot 检测）|
| 1 | UART_TX | SY7T609 通信（GPIO1）|
| 3 | UART_RX | SY7T609 通信（GPIO3）|
| 4 | BUTTON_PIN | 物理按键 |
| 5 | LED_WHITE_PIN | 从继电器指示灯 |
| 12 | RELAY_SLAVE_PIN | 分控继电器 |
| 14 | LED_RED_PIN | 红色 LED（WiFi 状态）|
| 15 | RELAY_ENABLE_PIN | 继电器锁存使能 |
| 16 | LED_BLUE_PIN | 蓝色 LED（通用指示）|

## 编译与刷入

### 环境要求
- [PlatformIO Core](https://docs.platformio.org/en/latest/core/) 或 VS Code + PlatformIO 插件
- Python 3.8+

### 编译固件
```bash
# 克隆项目
git clone https://github.com/S0DX/esp-powerstrip.git
cd esp-powerstrip

# 安装依赖
pio pkg install

# 编译
pio run -e esp12e
```

### 刷入固件

#### 首次刷入（USB）
```bash
# 修改 platformio.ini 中的 upload_port 为你的串口
pio run -e esp12e -t upload
```

#### OTA 升级（WiFi）
```bash
# 确保设备在局域网内，修改 upload_port 为设备 IP
pio run -e esp12e-ota -t upload --upload-port 192.168.1.100
```

### OTA 密码
- **Arduino OTA**（命令行）：`ota_password`
- **Web 固件升级**（网页）：`admin`

修改密码：编辑 `src/ota_mgr.cpp` 中的 `OTAManager::init()`

## 使用说明

### 初始配置

1. 首次上电，设备创建 AP `PowerStrip`（或 `PowerStrip-N`，无密码）
2. 连接后自动弹出配置门户（支持 iOS/Android/Windows）；或访问 `http://192.168.4.1`
3. 在"WiFi 状态"卡片中点击"连接 WiFi"配置网络
4. 连接成功后，设备 IP 会显示在界面上，也可通过 `power.local` 访问

### Web 界面功能

#### 继电器控制卡片
| 开关 | 作用 |
|------|------|
| 主继电器 | 控制总控电源 |
| 从继电器 | 控制分控电源 |
| 锁定模式 | 禁用物理按键 |

#### 实时电量卡片
- 电压、电流、功率、用电量实时显示
- 开关：启用/禁用电量检测

#### 用电历史卡片
- 7 天柱状图
- 底部三项数据（v5.0）：当前电量 / 本月用电 / 上月用电

#### 人来上电卡片
| 设置 | 作用 |
|------|------|
| 启用检测 | 开启 WiFi 信号检测 |
| 检测目标 | 选择要检测的 WiFi 名称 |
| 更多配置 | 按键检测、联锁倒计时、仅开主继电器、检测设置 |

#### 更多设置（二级页面）
- **主页卡片排序**：拖拽调整卡片顺序和显示
- **功耗优化**：拔除断电功率阈值配置（v5.0 合并自原"拔除断电设置"卡片）
- **倒计时关闭**：物理按钮自动倒计时开关
- **24小时循环**：多时段自动开关
- **计费供电**：电量/金额/时长阈值关闭，内置电价设置（v5.0 迁入）
- **电量检测卡片**（v5.0 迁入）：电压/电流校准、恢复出厂校准、保存、清零电能
- **MQTT 代理**：服务器配置
- **局域网域名**：mDNS 域名自定义
- **系统设置**：清零历史电量/电费记录、恢复出厂（v5.0 迁入清零功能）

### 物理按键操作

| 操作 | 功能 |
|------|------|
| 短按 | 继电器全关时开主继电器；主开从关时准备关主继电器；主从都开时关闭所有继电器 |
| 快速双按 | 第一次按下后的 200ms 内再按一次，同时开启从继电器 |
| 长按 20秒 | 恢复出厂设置 |

**锁定模式下**：
- 单击：触发按键检测（如果开启）
- 长按：关闭所有继电器

### LED 指示

| 状态 | 蓝色 LED | 白色 LED | 红色 LED |
|------|----------|----------|----------|
| 从继电器开启 | - | 常亮 | - |
| 倒计时运行中 | - | 闪烁 | - |
| WiFi 已连接 | - | - | 熄灭 |
| WiFi 断开 | - | - | 闪烁（可关闭）|
| 24小时循环开启 | 常亮 | - | - |

## API 接口

### 状态查询
```http
GET /api/status
```
返回完整设备状态（JSON）

### 继电器控制
```http
GET /api/relay?m=on&s=on    # 开启双继电器
GET /api/relay?m=off        # 关闭主继电器
GET /api/relay?s=off        # 关闭从继电器
```

### 定时器
```http
GET /api/timer?enabled=true&duration=2   # 开启2小时倒计时
GET /api/timer?enabled=false             # 关闭倒计时
```

### 人来上电
```http
GET /api/wifidetect?enabled=true         # 开启检测
GET /api/wifidetect?enabled=false        # 关闭检测
GET /api/wifidetect?linkTimer=true       # 开启联锁倒计时
GET /api/wifidetect?onlyMaster=true      # 仅开主继电器
```

### 按键检测
```http
GET /api/button_detect?enabled=true      # 开启按键检测
```

### mDNS 域名
```http
GET /api/mdns_hostname?name=mypower      # 设置域名为 mypower.local
```

### 其他接口
| 接口 | 方法 | 说明 |
|------|------|------|
| `/api/lock` | GET/POST | 锁定模式 |
| `/api/cycle` | GET/POST | 24小时循环 |
| `/api/meter` | GET/POST | 电能监测开关 |
| `/api/mqtt` | GET/POST | MQTT 配置 |
| `/api/poweroff` | GET/POST | 拔除断电设置 |
| `/api/billing` | GET/POST | 计费供电 |
| `/api/scan` | GET | 扫描 WiFi |
| `/api/connect` | POST | 连接 WiFi |
| `/api/history` | GET | 用电历史 |
| `/api/restart` | POST | 重启设备 |
| `/api/reset` | POST | 恢复出厂 |
| `/api/reset_energy` | POST | 清零电量记录 |

## UDP 设备发现

设备每 3 秒广播一次 JSON 数据到 `255.255.255.255:4210`：

```json
{
  "name": "PowerStrip",
  "ip": "192.168.1.100",
  "mac": "A4:CF:12:34:56:78",
  "ver": "5.0",
  "hostname": "power"
}
```

安卓 App 可监听此广播自动发现设备。

## 技术细节

### EEPROM 存储布局（v5.0）

| 地址 | 字节 | 功能 |
|------|------|------|
| 0-31 | 32 | WiFi SSID |
| 64-95 | 32 | WiFi 密码 |
| 128-139 | 12 | 自定义局域网域名 |
| 140-143 | 4 | 自动关AP / 系统日志 / WiFi功率限制 / 布局版本 |
| 195-197 | 3 | 倒计时定时器（分钟格式）|
| 199-209 | 11 | 锁定/红灯/循环定时器配置 |
| 210-211 | 2 | SY7T609 启用 / Flash 模式 |
| 212-217 | 6 | 拔除断电 + 计费供电基础配置 |
| 218-291 | 74 | 人来上电配置（目标/超时/RSSI/单价等）|
| 292-305 | 14 | 计费供电（起始电量/模式/阈值/时间）|
| 396-491 | 96 | 人来上电每小时统计 |
| 492-509 | 18 | 循环时段 + 卡片排序 |
| 510-511 | 2 | 卡片可见性 / 按钮自动定时 |
| 512-551 | 40 | 电量总计 + 7日历史 |
| 552-567 | 16 | 月度电量（本月/上月/前月）|
| 568-647 | 80 | 计费历史记录 |
| 648-724 | 77 | MQTT 配置 |
| 725 | 1 | 计费历史计数 |

### 控制逻辑优先级

```
1. 拔除断电（最高）→ 关闭继电器，不清除定时/循环
2. 手动控制 → 直接操作继电器
3. 24小时循环 ↔ 倒计时关闭（互斥）
```

### 调试输出
- 串口波特率：115200
- SY7T609 启用时：输出到 GPIO2 (Serial1)
- SY7T609 禁用时：输出到 Serial (GPIO1)

## 目录结构

```
esp-powerstrip/
├── src/                      # 源代码
│   ├── main.cpp             # 主程序
│   ├── config.h             # 配置定义（EEPROM 地址集中管理）
│   ├── gpio_mgr.cpp/h       # GPIO/继电器管理
│   ├── button_mgr.cpp/h     # 按键处理
│   ├── wifi_mgr.cpp/h       # WiFi 管理（含 mDNS）
│   ├── web_config.cpp/h     # Web 服务器（含 Captive Portal）
│   ├── sy7t609.cpp/h        # 电能芯片驱动（状态机非阻塞读取）
│   ├── sy7t609_def.h        # SY7T609 寄存器/校准常量定义
│   ├── energy_mgr.cpp/h     # 用电统计（延迟提交）
│   ├── mqtt_mgr.cpp/h       # MQTT 客户端
│   ├── ota_mgr.cpp/h        # OTA 升级
│   └── log_buffer.cpp/h     # 日志缓冲
├── cmpower-firmware/        # ESPHome 集成与固件分析
├── cmpower原理图.svg        # 硬件原理图
├── platformio.ini           # PlatformIO 配置
└── README.md                # 本文档
```

## 依赖库

- [PubSubClient](https://github.com/knolleary/pubsubclient) - MQTT 客户端
- [EspSoftwareSerial](https://github.com/plerup/espsoftwareserial) - 软件串口
- [ESP8266mDNS](https://github.com/esp8266/Arduino/tree/master/libraries/ESP8266mDNS) - mDNS 响应

## 许可证

MIT License

## 版本历史

- **v5.0** - UI 重构：电费计算卡片移除（电价迁入计费供电、清零迁入系统设置、电量校准迁入更多设置电量检测卡片）；拔除断电合并到功耗优化；用电历史底部添加当前/本月/上月三数据项；加载动画超时修复（fetch 5s 超时 + 3s 保底重试）；mDNS 可靠性提升（begin 重试 3 次 + update 提频）；Captive Portal 跨平台兼容（Android/Windows 探测端点）；SY7T609 readRegister 状态机化（主循环零阻塞）；EEPROM 地址冲突修复与清理
- v4.9 - WiFi 发射功率优化、RSSI 动态功率调整、EnergyManager 延迟提交
- v3.6 - 计费供电支持能量/金额/时长阈值及历史账单，人来上电"等待记录"初始状态修复，物理按钮单击/双击功能调整，AP 模式 Captive Portal 重定向到 power.local
- v3.5 - 新增 mDNS 局域网域名自定义（默认 power.local）
- v3.4 - 新增 UDP 设备发现广播
- v3.3 - 人来上电倒计时防重检、按键检测、更多配置二级页面
- v3.2 - 物理按钮自动倒计时、WiFi 睡眠模式优化
- v3.1 - 按钮双击响应优化
- v3.0 - 卡片排序、AP 后缀、WiFi 检测、继电器同步修复
