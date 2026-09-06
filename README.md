<div align="center">

# 🔌 智能插排固件

### ESP8266 Smart Power Strip Firmware · v5.1

<img src="docs/screenshots/banner.svg" alt="Smart Power Strip Banner" width="640"/>

**基于 ESP8266 (ESP-12E/F) 的智能插排固件** · 远程控制 / 电能计量 / 人来上电 / 计费供电 / Captive Portal

[![Version](https://img.shields.io/badge/version-5.1-007aff?style=flat-square)](#)
[![Platform](https://img.shields.io/badge/platform-ESP8266-34c759?style=flat-square)](#)
[![Language](https://img.shields.io/badge/language-C%2B%2B-orange?style=flat-square)](#)
[![License](https://img.shields.io/badge/license-MIT-blue?style=flat-square)](#)
[![PRs Welcome](https://img.shields.io/badge/PRs-welcome-ff69b4?style=flat-square)](#)

</div>

---

## 🌟 项目亮点

| 亮点 | 说明 |
|------|------|
| ⚡ **零阻塞状态机** | SY7T609 电能芯片读取采用状态机驱动，主循环永不阻塞（v5.0 重构） |
| 📱 **跨平台门户** | Captive Portal 支持 iOS / Android / Windows / macOS 自动弹窗 |
| 👋 **人来上电** | 检测指定 WiFi 设备信号自动通电，离开自动断电 |
| 💰 **计费供电** | 按电量/金额/时长三种模式，内置电价设置 |
| 📊 **电能可视化** | 实时 V/A/W/PF + 7 日柱状图 + 月度对比 |
| 🔄 **OTA 升级** | 支持 Arduino OTA / Web OTA 双通道，无需 USB |
| 🏠 **mDNS 域名** | 局域网 `power.local` 直达，免记 IP |

---

## 🖼️ 界面预览

| 主页控制 | 更多设置 |
| :---: | :---: |
| ![主页](docs/screenshots/main-page.svg) | ![更多设置](docs/screenshots/more-settings.svg) |
| 继电器控制 / 实时电量 / 用电历史 | 卡片排序 / 功耗优化 / 计费供电 / 电量校准 |

| 人来上电配置 | Captive Portal 跨平台门户 |
| :---: | :---: |
| ![人来上电](docs/screenshots/wifi-detect.svg) | ![Captive Portal](docs/screenshots/captive-portal.svg) |
| 目标选择 / 高级参数 / 工作机制 | iOS / Android / Windows / macOS 自动弹窗 |

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
- **24小时循环**：可视化 24 小时时间轴，最多 6 个时段，支持拖拽创建/调整、跨午夜时段及一键反向选择（v5.1）
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
git clone https://github.com/S0DX/esp8266-powerstrip.git
cd esp8266-powerstrip

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
局域网内任意位置均可升级，无需 USB 连接：
```bash
# 确保设备已联网，upload_port 改为设备 IP
pio run -e esp12e-ota -t upload --upload-port 192.168.1.100
```

#### Web OTA（推荐）

Web OTA 适合无命令行环境（如手机、临时电脑），操作步骤：

1. 设备连接 WiFi 后，访问 `http://power.local/` 或设备 IP
2. 进入"更多设置"页面，滚动到底部找到 **Web 固件升级** 卡片
3. 点击"选择固件"按钮，选取 `.pio/build/esp12e/firmware.bin` 文件
4. 点击"开始升级"，等待 1-2 分钟完成（期间切勿断电）
5. 升级完成后设备自动重启，新版本生效

#### OTA 密码

| 方式 | 密码 | 用途 |
|------|------|------|
| Arduino OTA（命令行 / PlatformIO） | `ota_password` | 局域网命令行固件推送 |
| Web OTA（网页上传） | `admin` | Web 界面固件上传 |
| mDNS 名称 | `power.local` | OTA 主机名（可在设置中修改） |

#### OTA 安全提示

- **务必保证电源稳定**：OTA 升级期间（~90 秒）不可断电，否则可能变砖需 USB 救砖
- **保留原固件备份**：升级前保留 `.pio/build/esp12e/firmware.bin` 旧版本
- **升级失败处理**：如设备卡在重启循环，连接 USB 重新刷入固件

#### 修改 OTA 密码

编辑 `src/ota_mgr.cpp` 中的 `OTAManager::init()`：

```cpp
ArduinoOTA.setPassword("ota_password");  // Arduino OTA 密码
// Web OTA 密码见 WebConfigServer::setup()
```

### Captive Portal 跨平台门户

首次上电未联网时，设备创建 AP，连接后自动弹出配置门户——无需手动输入 IP。v5.0 已支持全平台：

| 平台 | 探测端点 | 行为 |
|------|---------|------|
| iOS | `/hotspot-detect.html` | 弹出"登录网络"对话框 |
| Android | `/generate_204`、`/gen_204` | 通知栏提示"此网络需要登录" |
| Windows | `/connecttest.txt`、`/redirect` | 浏览器自动打开配置页 |
| macOS | `/hotspot-detect.html` | 弹窗（同 iOS） |

**触发条件**：
- 设备未连接任何 WiFi（首次配置或 STA 失败）
- AP 模式开启（默认 `PowerStrip` 或 `PowerStrip-N`）

**未弹出门户的手动访问**：浏览器地址栏输入 `http://192.168.4.1/` 或 `http://power.local/`。

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

##### 人来上电功能详解

**核心用途**：检测到指定手机/设备的 WiFi 信号（手机连接过家庭 WiFi 后会主动探测），自动开启继电器；人离开（信号消失）后自动关闭，实现"人到电通、人走电断"。

**典型场景**：
- 家中手机/平板检测：回家自动开灯开电器
- 办公室工位检测：坐下自动给电脑供电
- 智能家居联动：作为"人在家"信号触发其他设备

**启用步骤**：
1. 主页面"人来上电"卡片点击「选择WiFi」按钮
2. 从扫描结果中选择目标设备（手机、平板、IoT 设备）
3. 卡片显示目标名称和 MAC 地址
4. 点击「更多配置」调整参数后保存

**高级配置**（更多配置页）：

| 参数 | 默认值 | 说明 |
|------|--------|------|
| RSSI 阈值 | -75 dBm | 信号强度低于此值视为无人。建议 -80 ~ -70 |
| 扫描间隔（无目标） | 30 秒 | 未发现目标时的扫描周期，省电 |
| 扫描间隔（已发现） | 5 秒 | 已发现目标时高频确认，防止误判 |
| 联锁倒计时 | 关闭 | 信号消失后等待 N 秒再断电（避免瞬时信号波动） |
| 仅开主继电器 | 关闭 | 开启后只触发主继电器，从继电器保持关闭 |
| 按键检测 | 关闭 | 锁定模式下单击物理按钮触发单次扫描（适合 IoT 设备） |
| 拔除阈值功率 | 1W | 检测到目标但功率持续低于此值（拔除），自动关闭 |

**工作机制**：
```
周期扫描 → 发现目标 RSSI > 阈值 → 开启继电器（仅一次）
                                    ↓
                         周期性确认（每 5 秒）
                                    ↓
信号消失（连续 N 次）→ 触发联锁倒计时 → 倒计时结束 → 关闭继电器
```

**注意事项**：
- 目标设备必须**主动连接过家庭 WiFi**（即使不在线也会探测）
- 跨路由器的 WiFi 探测信号强度可能不足（建议同楼层）
- v3.6+ 修复了"等待记录"初始状态显示问题

#### 24小时循环（v5.1 重构）

采用可视化 24 小时时间轴交互（参考 Nest 调度设计），取代传统表单：

| 交互 | 操作 |
|------|------|
| 创建时段 | 在时间轴空白处按住拖动框选；原地轻点则创建默认 1 小时段 |
| 调整时段 | 拖动色块两端手柄（5 分钟吸附） |
| 编辑时段 | 轻点色块或列表"编辑"，弹出时间选择器（仅旧浏览器回退） |
| 跨午夜 | 结束时间早于开始时间即跨天，列表显示"↕ 跨天"徽章 |
| **反向选择** | 点击"↕ 跨天"徽章，时段立即反转（22:00-06:00 ⇄ 06:00-22:00） |
| 运行状态 | 实时显示：运行中(绿)/关闭时段(蓝)/等待NTP(橙)/未配置(橙)/未启用(灰) |

- 最多 6 个时段，时段间不允许重叠（前后端双重校验）
- 红色时刻线改为灰色 hairline，随轮询实时移动（Apple 风格）
- 启用按钮：未启用为蓝色"启用循环"，已启用为绿色"关闭循环"
- 不支持 Pointer Events 的浏览器自动回退为点击弹窗设置

#### 更多设置（二级页面）
v5.1 起按"功能性在前、维护性靠后"排序：
- **24小时循环**：可视化时间轴（见上文）
- **倒计时关闭**：物理按钮自动倒计时开关
- **计费供电**：电量/金额/时长阈值关闭，内置电价设置（v5.0 迁入）
- **功耗优化**：拔除断电功率阈值配置（v5.0 合并自原"拔除断电设置"卡片）
- **MQTT 代理**：服务器配置
- **电量检测卡片**（v5.0 迁入）：电压/电流校准、恢复出厂校准、保存、清零电能
- **主页卡片排序**：拖拽调整卡片顺序和显示
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
| 504-509 | 6 | 主页卡片排序 |
| 510-511 | 2 | 卡片可见性 / 按钮自动定时 |
| 512-551 | 40 | 电量总计 + 7日历史 |
| 552-567 | 16 | 月度电量（本月/上月/前月）|
| 568-647 | 80 | 计费历史记录 |
| 648-724 | 77 | MQTT 配置 |
| 725 | 1 | 计费历史计数 |
| 726-749 | 24 | 循环时段 V3（6 时段 × 4 字节，v5.1；旧地址 492-503 开机自动迁移）|

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
esp8266-powerstrip/
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

- **v5.1** - 24小时循环重构：可视化 24 小时时间轴（拖拽创建/调整、轻点编辑、跨午夜、一键反向选择）；时段上限 3→6（EEPROM V3 自动迁移）；运行状态实时反馈行；时段重叠前后端双重校验；循环启用改为蓝/绿状态按钮；二级页面卡片按功能性/维护性重排；循环轮询不再覆盖输入框（修复时段设置失效）
- **v5.0** - UI 重构：电费计算卡片移除（电价迁入计费供电、清零迁入系统设置、电量校准迁入更多设置电量检测卡片）；拔除断电合并到功耗优化；用电历史底部添加当前/本月/上月三数据项；加载动画超时修复（fetch 5s 超时 + 3s 保底重试）；mDNS 可靠性提升（begin 重试 3 次 + update 提频）；Captive Portal 跨平台兼容（Android/Windows 探测端点）；SY7T609 readRegister 状态机化（主循环零阻塞）；EEPROM 地址冲突修复与清理
- v4.9 - WiFi 发射功率优化、RSSI 动态功率调整、EnergyManager 延迟提交
- v3.6 - 计费供电支持能量/金额/时长阈值及历史账单，人来上电"等待记录"初始状态修复，物理按钮单击/双击功能调整，AP 模式 Captive Portal 重定向到 power.local
- v3.5 - 新增 mDNS 局域网域名自定义（默认 power.local）
- v3.4 - 新增 UDP 设备发现广播
- v3.3 - 人来上电倒计时防重检、按键检测、更多配置二级页面
- v3.2 - 物理按钮自动倒计时、WiFi 睡眠模式优化
- v3.1 - 按钮双击响应优化
- v3.0 - 卡片排序、AP 后缀、WiFi 检测、继电器同步修复
