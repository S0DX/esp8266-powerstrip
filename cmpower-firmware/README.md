# CMPOWER-W1 智能插排 ESPHome 固件

> **项目**: ROUTEROS_CN.CMPOWER-W1  
> **版本**: 6.2.0  
> **框架**: ESPHome 2026.5.0  
> **芯片**: ESP8266 (ESP-12E) + SY7T609 电能计量芯片  
> **固件来源**: 从 `cmpower-mqtt20260522mv2.bin` 反编译还原

---

## 目录

- [项目简介](#项目简介)
- [文件结构](#文件结构)
- [硬件架构](#硬件架构)
- [GPIO 引脚分配](#gpio-引脚分配)
- [功能详解](#功能详解)
- [编译指南](#编译指南)
- [配置说明](#配置说明)
- [使用指南](#使用指南)
- [校准指南](#校准指南)
- [保护机制](#保护机制)
- [SY7T609 组件 API](#sy7t609-组件-api)
- [SSI 通信协议](#ssi-通信协议)
- [常见问题](#常见问题)

---

## 项目简介

CMPOWER-W1 是一款基于 ESP8266 + SY7T609 的智能 WiFi 插排，具备以下核心能力：

- **电能计量**: 实时测量电压、电流、功率、功率因数、电量等 7 项电力参数
- **双路继电器控制**: 总控 + 分控两路继电器，支持独立控制和逻辑联动
- **三重安全保护**: 过压(>250V)、过流(>10A)、过温(>85°C) 自动断电保护
- **MQTT 集成**: 对接 Home Assistant，支持 MQTT 自动发现
- **Web 管理界面**: 内置 Web 服务器，支持网页端控制和配置
- **物理按键**: 单击/双击/长按三种操作手势
- **断电记忆**: 继电器状态和电量数据断电不丢失
- **OTA 升级**: 支持远程固件更新

---

## 文件结构

```
cmpower-firmware/
├── README.md                          # 本文档 - 项目说明与使用指南
├── cmpower-mqtt.yaml                  # ESPHome 主配置文件 (MQTT 版本)
├── 固件分析报告.md                     # 固件反编译分析报告
├── schematic.svg                      # 硬件原理图 (SVG 格式)
└── components/
    └── sy7t609/                        # SY7T609 自定义 ESPHome 组件
        ├── __init__.py                 # Python 包初始化文件
        ├── sensor.py                   # ESPHome 组件入口 (Schema + Action 注册)
        ├── sy7t609_def.h               # 寄存器地址表、协议常量、校准默认值
        ├── sy7t609_uart.h              # C++ 驱动类定义 + 6 种 Action 模板类
        └── sy7t609_uart.cpp            # C++ 驱动实现 (通信、状态机、校准)
```

### 文件说明

| 文件 | 作用 | 语言 |
|------|------|------|
| `cmpower-mqtt.yaml` | ESPHome 项目主配置，定义 WiFi/MQTT/WebServer/传感器/开关/按键等全部功能 | YAML |
| `sensor.py` | ESPHome 组件注册入口，定义 Schema 和 Action，被 ESPHome 编译器调用 | Python |
| `sy7t609_def.h` | 芯片寄存器地址映射、SSI 协议常量、校准参数默认值 | C++ |
| `sy7t609_uart.h` | 驱动类 `SY7T609_UART` 定义，6 个 ESPHome Action 模板类 | C++ |
| `sy7t609_uart.cpp` | 驱动实现：SSI 协议通信、表驱动轮询、状态机、校准逻辑 | C++ |

---

## 硬件架构

```
                    ┌─────────────┐
    AC 电源 ───────►│  SY7T609    │──── UART (9600bps) ────┐
                    │  电能计量    │                       │
                    │  芯片       │                       ▼
                    └──────┬──────┘              ┌────────────────┐
                           │                     │    ESP8266     │
                           ▼                     │   (ESP-12E)    │
                    ┌─────────────┐              │                │
                    │  总控继电器  │◄── GPIO0 ───│  WiFi + MQTT   │
                    │  (常通插孔)  │◄── GPIO15 ──│  Web Server    │
                    └──────┬──────┘   (触发使能) │  按键检测      │
                           │                     │  LED 控制      │
                    ┌─────────────┐              │  保护逻辑      │
                    │  分控继电器  │◄── GPIO12 ──│                │
                    │  (可控插孔)  │◄── GPIO15 ──│                │
                    └─────────────┘   (触发使能) └───────┬────────┘
                                                           │
                    ┌──────────┐  ┌──────────┐  ┌──────────┐
                    │ 蓝色 LED │  │ 红色 LED │  │ 白色 LED │
                    │ GPIO16   │  │ GPIO14   │  │ GPIO5    │
                    │ WiFi状态 │  │ 异常状态 │  │ 分控状态 │
                    └──────────┘  └──────────┘  └──────────┘
                                         ┌──────────┐
                                         │ 电源按键 │
                                         │ GPIO4    │
                                         └──────────┘
```

### 电气连接说明

- **AC 电源** 经过 SY7T609 采样后，一路到总控继电器（控制常通插孔），另一路到分控继电器（控制可控插孔）
- **继电器触发机制**: ESP8266 通过 GPIO 控制继电器线圈，同时通过 GPIO15 输出 10ms 脉冲触发继电器硬件锁存
- **UART 通信**: ESP8266 与 SY7T609 通过 UART 串口通信（TX=GPIO1, RX=GPIO3, 9600bps），使用 SSI 协议

---

## GPIO 引脚分配

| GPIO | 功能 | 组件 ID | 电气特性 | 说明 |
|------|------|---------|----------|------|
| GPIO0 | 主继电器控制 | `id_pin_relay_master` | inverted | 控制总控继电器线圈 |
| GPIO1 | UART TX | - | - | SY7T609 通信发送 |
| GPIO3 | UART RX | - | - | SY7T609 通信接收 |
| GPIO4 | 电源按键 | `id_power_key` | input, pullup, inverted | 物理按键输入 |
| GPIO5 | 白色 LED | `id_pin_led_white` | inverted | 分控继电器状态指示灯 |
| GPIO12 | 从继电器控制 | `id_pin_relay_slave` | inverted | 控制分控继电器线圈 |
| GPIO14 | 红色 LED | `id_pin_led_red` | inverted | WiFi/配网状态指示灯 |
| GPIO15 | 继电器触发使能 | `id_pin_trigger_relay_enable` | inverted | 10ms 脉冲触发继电器锁存 |
| GPIO16 | 蓝色 LED | `id_pin_led_blue` | inverted | WiFi 连接状态指示灯 |

---

## 功能详解

### 1. 电能计量 (SY7T609)

通过 UART 串口以 SSI 协议与 SY7T609 通信，轮询读取 7 项电力参数：

| 传感器 | 名称 | 单位 | 寄存器 | 解析方式 | 轮询间隔 |
|--------|------|------|--------|----------|----------|
| 电压 | 21.电压 | V | ADDR_VRMS (0x0033) | raw / 1000 | 3s |
| 电流 | 22.电流 | A | ADDR_IRMS (0x0036) | raw / 1000 | 3s |
| 有功功率 | 24.功率 | W | ADDR_POWER (0x0039) | signed raw / 1000 | 3s |
| 无功功率 | 25.无功功率 | VAR | ADDR_VAR (0x003C) | signed raw / 1000 | 3s |
| 功率因数 | 26.功率因数 | - | ADDR_PF (0x0048) | signed raw / 1000 | 3s |
| 芯片电量 | 28.重启后电量 | Wh | ADDR_EPPCNT (0x0069) | raw (直接返回) | 3s |
| 芯片温度 | 19.芯片温度 | °C | ADDR_CTEMP (0x0027) | raw / 1000 | 3s |

**表驱动设计**: 所有测量项在 `get_measurements_()` 注册表中定义，新增测量项只需在表中加一行。

### 2. 双路继电器控制

采用 **逻辑开关 + 真实开关** 双层架构：

| 开关类型 | 名称 | 组件 ID | 面向 | 说明 |
|----------|------|---------|------|------|
| 总控逻辑开关 | 32.总控开关 | `id_relay_master_logic` | 用户 | 控制常通插孔通断 |
| 分控逻辑开关 | 33.分控开关 | `id_relay_slave_logic` | 用户 | 控制可控插孔通断 |
| 总控真实开关 | (内部) | `id_relay_master_real` | 内部 | 直接控制 GPIO0 继电器 |
| 分控真实开关 | (内部) | `id_relay_slave_real` | 内部 | 直接控制 GPIO12 继电器 |

**设计原因**: 两个继电器可独立控制，可能出现"主继电器断开但从继电器仍显示开启"的误导状态。逻辑开关确保分控只有在总控接通时才显示"开启"。

**继电器触发流程**: 每次开关动作 → 控制 GPIO 电平 → GPIO15 输出 10ms 脉冲触发硬件锁存 → 更新白色 LED 指示灯。

### 3. 电度量持久化

SY7T609 的电量计数器是内存存储，断电会丢失。固件通过两层机制实现断电记忆：

- **日电量** (`id_total_daily_energy_median`): 使用 ESPHome `total_daily_energy` 组件，基于 SNTP 时间每日 0 点自动归零，`restore: true` 启用 flash 持久化
- **总电量** (`id_sensor_energy_counter_persist`): 使用 `copy` 传感器 + `globals` 全局变量，每次电量变化时计算增量并累加到 `id_energy_counter_persist`（`restore_value: yes` 启用 flash 持久化），`flash_write_interval: 300s` 控制写入频率

### 4. LED 状态指示

| LED 颜色 | GPIO | 状态 | 含义 |
|----------|------|------|------|
| 蓝色 | GPIO16 | 常亮 | WiFi 已连接 |
| 蓝色 | GPIO16 | 熄灭 | WiFi 未连接或已断开 |
| 蓝色 | GPIO16 | 闪烁 | 配网模式 (Captive Portal) |
| 红色 | GPIO14 | 常亮 | Captive Portal 激活且 WiFi 未连接 |
| 红色 | GPIO14 | 闪烁 | WiFi 连接异常 |
| 白色 | GPIO5 | 常亮 | 分控继电器已接通且总控已接通 |
| 白色 | GPIO5 | 熄灭 | 分控继电器断开或总控断开 |

### 5. 物理按键操作

按键连接在 GPIO4，支持三种操作手势（需启用"42.启用按键"开关）：

| 操作 | 时间判定 | 动作 |
|------|----------|------|
| 单击 | ON < 0.3s, OFF > 0.2s | 切换分控开关 |
| 双击 | ON < 0.5s, OFF < 0.5s, ON < 0.5s, OFF > 0.2s | 切换总控开关 |
| 长按 | ON > 6s | 恢复出厂设置（清零电量 + 进入配网模式） |

### 6. 网络通信

- **MQTT**: 连接 Home Assistant MQTT Broker，启用自动发现（`discovery_prefix: homeassistant`），设备状态通过 `cmpower/status` 主题上报
- **Web 服务器**: 内置 ESPHome Web Server v3（端口 80），支持网页端查看传感器数据和控制开关
- **WiFi 配网**: 支持 Captive Portal 热点配网，WiFi 连接失败 60 秒后自动开启 AP 热点
- **SNTP 时间同步**: 使用阿里云 NTP 服务器同步时间，用于日电量每日归零

---

## 编译指南

### 环境要求

- ESPHome >= 2026.2.0
- Python >= 3.9
- 网络: 首次编译需要访问 GitHub 下载 ESPHome 框架和依赖库

### 编译步骤

1. **安装 ESPHome** (如果尚未安装):
   ```bash
   pip install esphome
   ```

2. **配置 WiFi**: 编辑 `cmpower-mqtt.yaml`，找到 `wifi:` 段，取消注释并填写 WiFi 信息:
   ```yaml
   wifi:
     ssid: "你的WiFi名称"
     password: "你的WiFi密码"
   ```

3. **配置 MQTT** (如需要): 编辑 `cmpower-mqtt.yaml`，找到 `mqtt:` 段，修改 broker 地址:
   ```yaml
   mqtt:
     broker: "你的MQTT服务器IP"
     port: 1883
     username: "你的用户名"
     password: "你的密码"
   ```

4. **编译固件**:
   ```bash
   esphome compile cmpower-mqtt.yaml
   ```

5. **烧录固件** (首次使用 USB 线连接):
   ```bash
   esphome upload cmpower-mqtt.yaml
   ```

6. **后续 OTA 升级** (设备已连接 WiFi 后):
   ```bash
   esphome upload cmpower-mqtt.yaml --device 192.168.x.x
   ```

---

## 配置说明

### 关键配置项

| 配置项 | 位置 | 默认值 | 说明 |
|--------|------|--------|------|
| WiFi SSID/密码 | `wifi:` 段 | 未设置 | 需用户填写 |
| MQTT Broker | `mqtt:` 段 | 192.168.1.11 | 按实际环境修改 |
| MQTT 账号密码 | `mqtt:` 段 | admin/admin888 | 按实际环境修改 |
| Web 服务器账号 | `web_server:` 段 | admin/admin888 | 建议修改默认密码 |
| UART 波特率 | `uart:` 段 | 9600 | 不可修改，SY7T609 固定 9600 |
| 传感器轮询间隔 | `sensor:` 段 | 3s | 可调，但不建议低于 2s |
| Flash 写入间隔 | `preferences:` 段 | 300s | 控制电量持久化写入频率 |
| 过压阈值 | `sensor.voltage.on_value_range` | 250V | 可调 |
| 过流阈值 | `sensor.current.on_value_range` | 10A | 可调 |
| 过温阈值 | `sensor.chip_temperature.on_value_range` | 85°C | 可调 |

### Web 服务器分组

Web 界面中的实体按 4 个分组展示：

| 分组 | sorting_weight | 包含的实体 |
|------|----------------|------------|
| 诊断 | 10 | 系统运行时间、WiFi信号、芯片温度、系统讯息、网络信息 |
| 传感器 | 20 | 电压、电流、功率、无功功率、功率因数、日电量、总电量 |
| 控制 | 30 | 总控开关、分控开关、延时重启 |
| 配置 | 40 | 保护开关、按键开关、操作确认、校准/重置按钮、重启 |

---

## 使用指南

### 首次使用

1. 烧录固件到 ESP8266
2. 设备上电后，WiFi 未配置会自动进入配网模式（AP 热点）
3. 手机连接设备 AP 热点，在弹出页面中配置 WiFi
4. 配置成功后设备自动重启并连接 WiFi
5. 通过 Web 浏览器访问设备 IP（默认端口 80，账号 admin/admin888）
6. 在 Home Assistant 中添加 MQTT 集成，设备会自动发现

### 日常操作

| 操作方式 | 方法 |
|----------|------|
| 物理按键 | 单击=切换分控，双击=切换总控，长按6秒=恢复出厂 |
| Web 界面 | 浏览器访问设备 IP，在"控制"分组操作开关 |
| Home Assistant | 通过 MQTT 自动发现的实体控制 |
| MQTT 命令 | 直接发布 MQTT 消息到对应主题 |

### 操作确认机制

"44.启用下面的四个操作" 开关是一个安全机制：
- 开启后 1 分钟内可执行以下操作：重置总电量、重置日电量、校准电力数据、恢复出厂
- 1 分钟后自动关闭，防止误操作
- 每次执行操作后也会自动关闭

---

## 校准指南

### 校准类型

| 校准动作 | 用途 | 使用场景 |
|----------|------|----------|
| `sy7t609.reset_energy` | 清零电量计数器 | 需要重新计量电量时 |
| `sy7t609.reset_calibration` | 恢复出厂校准值 | 校准异常时恢复默认 |
| `sy7t609.calibrate_voltage` | 电压自动校准 | 用标准电压源校准电压测量 |
| `sy7t609.calibrate_current` | 电流自动校准 | 用标准电流源校准电流测量 |
| `sy7t609.calibrate` | 通用寄存器校准 | 功率/相位等高级校准 |
| `sy7t609.save_calibration` | 保存校准到 Flash | 手动触发保存 |

### 电压校准流程

1. 将插排接入已知的标准电压源（如 220V）
2. 通过 Web 界面或 MQTT 触发校准动作:
   ```yaml
   # 在 YAML 中定义（可选）
   on_press:
     - sy7t609.calibrate_voltage:
         id: id_sensor_sy7t609
         value: 220.0  # 标准电压值 (V)
   ```
3. 芯片会自动计算 VGAIN 增益并保存到 Flash

### 电流校准流程

1. 在插排上接入已知功率的负载（如 1000W 电水壶）
2. 用万用表测量实际电流值
3. 触发校准动作:
   ```yaml
   - sy7t609.calibrate_current:
       id: id_sensor_sy7t609
       value: 4.545  # 实际电流值 (A) = 1000W / 220V
   ```

### 默认校准参数

以下参数定义在 `sy7t609_def.h` 中，`reset_calibration` 动作会写入这些值：

| 参数 | 寄存器 | 默认值 | 说明 |
|------|--------|--------|------|
| IGAIN | 0x00D5 | 0x43DF0D | 电流增益（万用表校准值） |
| VGAIN | 0x00D8 | 0x210A40 | 电压增益 |
| ISCALE | 0x00ED | 0x00F230 | 电流缩放 |
| VSCALE | 0x00F0 | 0x0A2D78 | 电压缩放 |
| PSCALE | 0x00F3 | 0x027703 | 功率缩放 |
| ACCUM | 0x0105 | 0x001A2C | 累加系数 |
| IRMS_TARGET | 0x0117 | 0x0003E8 | 电流校准目标 (1.000A) |
| VRMS_TARGET | 0x011A | 0x03807C | 电压校准目标 |
| POWER_TARGET | 0x011D | 0x01D4C0 | 功率校准目标 |
| CONTROL | 0x0006 | 0x001817 | 控制寄存器 |
| BUCKETL | 0x00C0 | 0x7780A5 | 累加桶低位（原厂默认） |
| BUCKETH | 0x00C3 | 0x000247 | 累加桶高位 |

---

## 保护机制

### 三重安全保护

| 保护类型 | 触发条件 | 动作 | 恢复方式 | 可否禁用 |
|----------|----------|------|----------|----------|
| 过压保护 | 电压 > 250V | 立即断开总控开关 | 30 秒后自动检测，电压 <= 250V 则恢复 | 是 |
| 过流保护 | 电流 > 10A | 立即断开总控开关 | 手动恢复 | 是 |
| 过温保护 | 芯片温度 > 85°C | 立即断开总控开关 | 手动恢复 | 是 |

### 过压自动恢复脚本

`overvoltage_recovery_script` 的工作流程：
1. 电压超过 250V → 断开总控开关 → 启动恢复脚本
2. 脚本等待 30 秒
3. 检测当前电压：若 <= 250V → 恢复总控开关并发布"过压解除"消息
4. 若仍 > 250V → 递归执行脚本，继续等待 30 秒

### 保护开关

"41.启用过流过压过温保护" 开关控制保护功能的启用/禁用：
- 默认开启（`RESTORE_DEFAULT_ON`）
- 关闭后所有保护逻辑不再触发
- 断电后恢复上次状态

---

## SY7T609 组件 API

### YAML 配置 Schema

```yaml
sensor:
  - platform: sy7t609
    id: <组件ID>                    # 必填，全局唯一标识
    # 以下均为可选传感器子项
    power_factor:                   # 功率因数，精度2位
      name: "功率因数"
    voltage:                        # 电压 (V)，精度1位
      name: "电压"
    current:                        # 电流 (A)，精度3位
      name: "电流"
    power:                          # 有功功率 (W)，精度2位
      name: "功率"
    reactive_power:                 # 无功功率 (VAR)，精度2位
      name: "无功功率"
    energy:                         # 电量计数器 (Wh)，精度2位
      name: "电量"
    chip_temperature:               # 芯片温度 (°C)，精度1位
      name: "芯片温度"
    update_interval: 3s             # 轮询间隔，默认 60s
```

### ESPHome Action 用法

#### 1. 重置电量计数器
```yaml
- sy7t609.reset_energy: id_sensor_sy7t609
```

#### 2. 重置校准参数（恢复出厂校准值）
```yaml
- sy7t609.reset_calibration: id_sensor_sy7t609
```

#### 3. 电压自动校准
```yaml
- sy7t609.calibrate_voltage:
    id: id_sensor_sy7t609
    value: 220.0    # 标准电压值 (V)
```

#### 4. 电流自动校准
```yaml
- sy7t609.calibrate_current:
    id: id_sensor_sy7t609
    value: 4.545    # 标准电流值 (A)
```

#### 5. 通用校准（高级）
```yaml
- sy7t609.calibrate:
    id: id_sensor_sy7t609
    command: 0xCA0020       # COMMAND 寄存器命令码
    target_register: 0x011A  # 目标寄存器地址（默认 0x0FFF=不写寄存器）
    value: 0x03807C          # 写入值（24bit，默认 0）
```

#### 6. 保存校准到 Flash
```yaml
- sy7t609.save_calibration: id_sensor_sy7t609
```

---

## SSI 通信协议

SY7T609 使用 SSI (Serial Slave Interface) 协议通过 UART 通信。

### 帧格式

```
┌────────┬────────┬──────────┬───────────────┬──────────┬──────────┐
│ 帧头   │ 长度   │ 命令字   │ 寄存器地址    │ 数据     │ 校验和   │
│ 0xAA   │ 1 byte │ 1 byte   │ 2 bytes (LE)  │ 0/3 byte │ 1 byte   │
└────────┴────────┴──────────┴───────────────┴──────────┴──────────┘
```

### 命令字

| 命令 | 代码 | 说明 |
|------|------|------|
| CMD_SELECT_REGISTER_ADDRESS | 0xA3 | 选择寄存器地址（读/写前必须先发送） |
| CMD_READ_REGITSTER_3BYTES | 0xE3 | 读取当前选中寄存器的 3 字节数据 |
| CMD_WRITE_RETISTER_3BYTES | 0xD3 | 向当前选中寄存器写入 3 字节数据 |

### 读操作流程

1. **发送读请求** (7 字节):
   ```
   [0xAA, 0x07, 0xA3, addr_lo, addr_hi, 0xE3, checksum]
   ```
2. **接收回复** (6 字节):
   ```
   [0xAA, 0x06, 0x00, data_lo, data_mid, data_hi, checksum]
   ```
   注意：回复帧第 3 字节为 0x00（状态码），数据在字节 [3][4][5]（小端序）

### 写操作流程

1. **发送写请求** (10 字节):
   ```
   [0xAA, 0x0A, 0xA3, addr_lo, addr_hi, 0xD3, val_lo, val_mid, val_hi, checksum]
   ```
2. **接收回复** (3 字节):
   ```
   [0xAD, 0x03, checksum]  或  [0xAA, 0x03, checksum]
   ```

### 校验和算法

```cpp
uint8_t checksum = 0;
for (int i = 0; i < length - 1; i++) {
    checksum += frame[i];
}
checksum = ~checksum + 1;  // 二进制补码
```

### 数据编码

- **无符号值** (电压、电流、温度): `工程量 = raw / 1000`
- **有符号值** (功率因数、功率、无功功率): 24bit 二进制补码，`raw >= 0x800000` 时为负数
  - 正值 = 正向用电（吸收功率）
  - 负值 = 反向回馈（如光伏倒送电网）
- **电量计数器**: 直接返回原始计数值，不除以 1000

---

## 常见问题

### Q: 固件烧录后无法连接 WiFi？

1. 检查 `cmpower-mqtt.yaml` 中 WiFi SSID 和密码是否正确
2. 检查 WiFi 认证模式是否满足 `min_auth_mode: WPA2`（不支持 WEP/Open）
3. 长按按键 6 秒进入配网模式，通过 Captive Portal 重新配置

### Q: 传感器数据不更新？

1. 检查 UART 接线是否正确（TX=GPIO1, RX=GPIO3）
2. 检查 SY7T609 芯片是否正常供电（3.3V）
3. 查看 ESPHome 日志（`esphome logs cmpower-mqtt.yaml`）是否有通信错误
4. 尝试执行"45.校准电力数据"恢复出厂校准值

### Q: 电量数据断电后丢失？

总电量已通过 ESP8266 flash 持久化（`id_energy_counter_persist`），`flash_write_interval: 300s`。最坏情况下断电会丢失最近 5 分钟的电量数据。如需更高可靠性，可缩短 `flash_write_interval`（但会增加 flash 磨损）。

### Q: 继电器不动作？

1. 检查 GPIO0 (主继电器) 和 GPIO12 (从继电器) 的 inverted 设置
2. 确认 GPIO15 (触发使能) 能输出 10ms 脉冲
3. 检查继电器硬件电路是否正常

### Q: MQTT 无法连接 Home Assistant？

1. 检查 MQTT broker 地址、端口、账号密码
2. 确认 Home Assistant MQTT 集成已启用
3. 检查网络连通性（设备与 broker 在同一网络）
4. `reboot_timeout: 0s` 表示 MQTT 断开后不会自动重启设备

### Q: 如何修改保护阈值？

编辑 `cmpower-mqtt.yaml` 中对应传感器的 `on_value_range` 条件：
```yaml
# 例如将过压阈值改为 260V
voltage:
  on_value_range:
    - above: 260    # 原值 250
```

### Q: 如何切换为 API 模式（替代 MQTT）？

将 `cmpower-mqtt.yaml` 中的 `mqtt:` 段替换为:
```yaml
api:
  reboot_timeout: 0s
```
然后重新编译烧录。

---

## 技术支持

- **项目**: ROUTEROS_CN.CMPOWER-W1
- **版本**: 6.2.0
- **固件来源**: `cmpower-mqtt20260522mv2.bin` 反编译还原
- **ESPHome 文档**: https://esphome.io/
- **SY7T609 数据手册**: 参见芯片厂商官方文档
