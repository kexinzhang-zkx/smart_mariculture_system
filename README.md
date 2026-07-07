# 智能海洋水产养殖系统

> 基于 GEC6818 + STM32F103 + ESP8266 + 阿里云 + DeepSeek-R1

## 项目概述

本项目构建了一套完整的智能海洋水产养殖系统，实现从传感器数据采集、多协议传输、边缘计算、PID 闭环控制、人机交互到云端 AI 决策的全链路闭环。

### 核心功能

| 模块 | 功能 | 实现方式 |
|------|------|---------|
| 多源环境感知 | 温湿度、光照、溶解氧、pH、浊度 | DHT11 + ADC + 软件模拟 |
| 多协议传输网关 | WiFi → MQTT → 阿里云 | ESP8266 AT 指令 + Mosquitto |
| 边缘数据预处理 | 去重、异常过滤、质量评分、持久化 | 滑动窗口 + 阈值检测 |
| PID+PWM 闭环控制 | 增氧机、循环水泵、投喂电机 | 位置式 PID + sysfs PWM |
| 人机界面 | 7 屏 LVGL 触摸交互 | LVGL 9.1 + FreeType 中文 |
| 云端 AI 决策 | 一键评价、故障诊断、智能建议、运维回答 | Ollama + DeepSeek-R1:1.5B |

---

## 系统架构

```
┌─────────────────────┐         MQTT/JSON          ┌──────────────────────────┐
│   STM32F103 (采集)   │ ──────────────────────────→ │   阿里云 ECS              │
│                     │   topic: auaculture_sensor   │   Mosquitto Broker       │
│  DHT11 → PA15       │                             │   118.178.236.156:1883   │
│  光敏 → PA5(ADC)     │                             └───────────┬──────────────┘
│  DO/pH/浊度(模拟)    │                                         │
│  ESP8266 → USART1   │                               mosquitto_sub
└─────────────────────┘                                         ↓
                                                     ┌──────────────────────────┐
                                                     │   GEC6818 ARM Linux       │
                                                     │   (边缘计算 + 控制)        │
                                                     │                          │
                                                     │  ┌────────────────────┐  │
                                                     │  │ 数据预处理          │  │
                                                     │  │ 去重/异常/质量标签   │  │
                                                     │  └─────────┬──────────┘  │
                                                     │            ↓             │
                                                     │  ┌────────────────────┐  │
                                                     │  │ PID 闭环控制        │  │
                           HTTP POST                 │  │ 增氧机/水泵/投喂     │  │
                           /api/generate             │  │ PWM sysfs 输出      │  │
  ┌──────────────────┐ ←─────────────────────────── │  └─────────┬──────────┘  │
  │  PC (Ollama)     │                               │            ↓             │
  │  DeepSeek-R1:1.5B│                               │  ┌────────────────────┐  │
  │  192.168.29.23   │                               │  │ LVGL 7屏触控界面    │  │
  │  :11434          │                               │  │ PID/AI/数据/传感器   │  │
  └──────────────────┘                               │  └────────────────────┘  │
                                                     └──────────────────────────┘
```

---

## 硬件清单

| 设备 | 型号/规格 | 用途 |
|------|----------|------|
| ARM Linux 板 | GEC6818 (S5P6818) | 边缘计算核心 |
| MCU | STM32F103C8T6 | 传感器数据采集 |
| WiFi 模块 | ESP8266 (AT 固件) | MQTT 无线传输 |
| 温湿度传感器 | DHT11 | 空气温湿度 |
| 光敏电阻 | GL5528 | 光照强度 |
| 触摸屏 | 7" 800×480 | LVGL 交互界面 |

---

## 软件栈

| 层次 | 技术 | 版本 |
|------|------|------|
| GUI | LVGL | 9.1.0 |
| 字体 | FreeType | 2.x |
| AI 模型 | DeepSeek-R1 | 1.5B (Ollama) |
| MQTT Broker | Mosquitto | 2.x |
| STM32 库 | SPL (Standard Peripheral Library) | 3.5 |
| 编译链 | GCC (arm-linux-gcc / arm-none-eabi-gcc) | — |

---

## 目录结构

```
project/
├── lv_port_linux_sdl_gec6818/       # GEC6818 主工程
│   ├── main.c                        # 入口：LVGL + MQTT订阅 + 控制线程
│   ├── Makefile                      # 交叉编译脚本
│   ├── control/
│   │   ├── auto_control.c/h          # 控制引擎：采集→判断→PID→PWM 闭环
│   │   ├── pid_controller.c/h        # PID 算法：位置式 + 抗饱和
│   │   └── pwm_driver.c/h            # PWM 驱动：Linux sysfs 接口
│   ├── ui/
│   │   ├── ui.c/h                    # LVGL 总入口 + 屏幕导航事件
│   │   ├── ui_events.c/h             # 全部业务逻辑事件处理
│   │   └── screens/
│   │       ├── ui_Screen1.c          # 登录/注册
│   │       ├── ui_Screen2.c          # PID 自动化控制面板
│   │       ├── ui_Screen4.c          # AI 智能决策辅助
│   │       ├── ui_Screen5.c          # 数据预处理分析
│   │       ├── ui_Screen6.c          # 传感器实时监控 + 折线图
│   │       └── ui_Screen7.c          # 导航菜单
│   └── comm/
│       └── cloud_mqtt.c/h            # MQTT 模块（已废弃，功能移入main.c）
│
├── 01/                               # STM32 工程
│   ├── User/main.c                   # 主程序（DHT11 + ADC + ESP8266 + MQTT）
│   ├── User/stm32f10x_it.c           # 中断服务函数
│   ├── system/
│   │   ├── dht11.c/h                 # DHT11 单总线驱动
│   │   ├── adc_light.c/h             # ADC 光敏电阻驱动
│   │   ├── sensor.c/h                # 传感器数据汇总 + JSON 封装
│   │   ├── esp8266.c/h               # ESP8266 AT 指令驱动
│   │   ├── mqtt.c/h                  # MQTT 原始协议包构建器（预留）
│   │   ├── dwt_delay.c/h             # DWT 微秒延时
│   │   └── Delay.c/h                 # 毫秒延时
│   ├── Library/                      # STM32 标准外设库
│   └── Start/                        # 启动文件 + 系统时钟
│
├── STM32发布数据到MQTT完整代码V1(1)/  # 参考代码
├── 论文_智能海洋水产养殖系统.md        # 毕业设计论文
├── 系统知识点与流程详解.md             # 知识点与流程文档
└── README.md                          # 本文件
```

---

## 快速开始

### 1. STM32 端部署

**环境要求：**
- Keil MDK 5
- J-Link / ST-Link 烧录器
- `01/` 为完整 Keil 工程

**步骤：**
1. 用 Keil 打开 `01/` 工程
2. 确认 `WIFI_SSID` / `WIFI_PWD` 配置正确
3. 编译 → 烧录到 STM32F103

**关键配置** (`01/User/main.c`)：
```c
#define WIFI_SSID       "sunbao"
#define WIFI_PWD        "12345678"
#define MQTT_BROKER_IP  "118.178.236.156"
#define MQTT_TOPIC      "auaculture_sensor"
```

### 2. 阿里云 ECS 部署

```bash
# 安装 Mosquitto
sudo apt install mosquitto mosquitto-clients -y

# 配置匿名访问
sudo tee -a /etc/mosquitto/mosquitto.conf <<EOF
listener 1883 0.0.0.0
allow_anonymous true
EOF

# 启动
sudo systemctl restart mosquitto

# 验证
mosquitto_sub -t "auaculture_sensor" -h localhost -C 1
```

### 3. GEC6818 端部署

```bash
# 交叉编译
cd lv_port_linux_sdl_gec6818/
make clean && make

# 部署到 GEC6818
scp ./demo root@<gec6818_ip>:/root/

# 在 GEC6818 上运行
./demo
```

### 4. AI 服务端部署 (PC)

```bash
# 安装 Ollama
curl -fsSL https://ollama.com/install.sh | sh

# 拉取模型
ollama pull deepseek-r1:1.5b

# 启动（监听所有网口）
OLLAMA_HOST=0.0.0.0 ollama serve

# 验证
curl -X POST http://localhost:11434/api/generate \
  -H "Content-Type: application/json" \
  -d '{"model":"deepseek-r1:1.5b","prompt":"hello","stream":false}'
```

### 5. 系统启动顺序

```
① PC (Ollama)        → 启动 deepseek-r1:1.5b
② 阿里云 ECS          → 启动 Mosquitto
③ STM32 + ESP8266    → 上电自动连接 WiFi 并发布数据
④ GEC6818            → ./demo 启动，自动订阅 MQTT
```

---

## LVGL 屏幕导航

```
Screen1 (登录/注册)
    │
    ↓ 登录成功
Screen7 (导航菜单)
    ├──→ Screen4 (AI 智能决策辅助)
    │      ┌─ 一键评价：注入 DO/T/pH/浊度/光照 → 打分 1-10
    │      ├─ 故障诊断：注入设备状态 → 判断异常
    │      ├─ 智能建议：注入水质数据 → 管理建议
    │      └─ 运维回答：注入运行时长 → 维护指导
    │
    ├──→ Screen5 (数据预处理分析)
    │      ┌─ Container6: 实时数据质量 (OK/DUP/ERR/LOW/HIGH)
    │      ├─ Container8: 24h 统计 (max/min/avg/趋势)
    │      ├─ Container9: 趋势预测
    │      └─ Container10: 存储状态 (eMMC/条数/空间)
    │
    ├──→ Screen2 (PID 自动化控制面板)
    │      ┌─ Container1: 设备状态 (3个开关 + 占空比)
    │      ├─ Container4: PID 参数调节 (3设备 × 6按钮)
    │      └─ Container5: 实时数据 + 目标值调节
    │
    └──→ Screen6 (传感器实时监控)
           ┌─ Container3: 6路传感器数值
           ├─ 实时折线图 (6系列, 30点历史)
           └─ 彩色图例
```

---

## PID 控制参数

| 设备 | Kp | Ki | Kd | 目标值 | 控制周期 | 控制方式 |
|------|----|----|-----|--------|---------|---------|
| 增氧机 | 1.5 | 0.3 | 0.05 | DO→5.0 mg/L | 200ms | PID 闭环 |
| 循环水泵 | 1.0 | 0.2 | 0 | T→25.0°C | 200ms | PID 闭环 |
| 投喂电机 | 0.8 | 0.1 | 0 | 60s/次 | 200ms | 定时器 |

**PID 特性：**
- 位置式 PID + 积分分离（误差 > 30% 关积分）
- 死区 0.5%（避免微调磨损）
- 输出限幅 (0~100%) + 遇限削弱积分
- Screen2 在线调参（Kp±0.1, Ki±0.05, Kd±0.01）

---

## PWM 通道映射

| 设备 | PWM 通道 | sysfs 路径 |
|------|---------|-----------|
| 增氧机 | PWM0 | `/sys/class/pwm/pwmchip0/pwm0/` |
| 投喂电机 | PWM1 | `/sys/class/pwm/pwmchip0/pwm1/` |
| 循环水泵 | PWM2 | `/sys/class/pwm/pwmchip0/pwm2/` |

- PWM 频率：1kHz (`period = 1,000,000 ns`)
- 占空比 = `duty_cycle / period × 100%`

---

## MQTT 数据格式

**Topic:** `auaculture_sensor`

**Payload (JSON):**
```json
{
    "temp":  29.7,
    "humi":  67.4,
    "light": 1091,
    "do":    7.80,
    "ph":    8.01,
    "turb":  42.6
}
```

**字段说明：**

| 字段 | 含义 | 单位 | 来源 |
|------|------|------|------|
| temp | 空气温度 | °C | DHT11 |
| humi | 空气湿度 | %RH | DHT11 |
| light | 光照强度 | lux | ADC 光敏电阻 |
| do | 溶解氧 | mg/L | 软件模拟 |
| ph | pH 值 | — | 软件模拟 |
| turb | 浊度 | NTU | 软件模拟 |

---

## 数据预处理规则

### 质量标签优先级

```
传感器有效性 > 物理范围 (ERR) > 去重 (DUP) > 养殖阈值 (LOW/HIGH) > OK
```

### 物理范围约束

| 参数 | 范围 | 超出判定 |
|------|------|---------|
| 溶解氧 | 0.1 ~ 20.0 mg/L | 传感器故障 |
| 水温 | 0.0 ~ 50.0 °C | 传感器故障 |
| pH | 0.0 ~ 14.0 | 传感器故障 |
| 浊度 | 0.0 ~ 200.0 NTU | 传感器故障 |

### 养殖阈值

| 参数 | 低阈值 | 高阈值 | 目标值 |
|------|--------|--------|--------|
| 溶解氧 | 2.0 mg/L | 9.0 mg/L | 5.0 mg/L |
| 水温 | 15.0 °C | 35.0 °C | 25.0 °C |

---

## AI Prompt 设计

| 按钮 | 注入数据 | 任务指令 |
|------|---------|---------|
| 一键评价 | DO, T, pH, Turb, Lux | 评价水质,30字 |
| 故障诊断 | 增氧%, 泵%, 投喂状态, DO, T | 有故障吗?20字 |
| 智能建议 | DO, T, pH, Turb | 养殖建议,30字 |
| 运维回答 | 三设备运行时间 + 占空比 | 维护建议,30字 |

---

## 数据持久化

- **文件路径：** `./sensor.log`
- **格式：** CSV（逗号分隔）
- **写入频率：** 每 30 秒
- **记录示例：**
  ```
  26.3,67.4,5.2,7.90,42.6,1091
  ```
- **存储位置：** GEC6818 eMMC

---

## 调试日志

| 日志标签 | 来源 | 含义 |
|---------|------|------|
| `[MQTT]` | main.c | MQTT 订阅状态/延迟/接收数据 |
| `[CTRL]` | auto_control.c | 控制循环：DO值/占空比/异常 |
| `[SYS]` | ui_events.c | 系统摘要（每 60 秒） |
| `[AI]` | ui_events.c | AI 请求/响应状态 |
| `[PWM]` | pwm_driver.c | PWM 初始化状态 |
| `[LOGIN]` | ui_events.c | 登录/注册操作 |
| `[TARGET]` | ui_events.c | Screen2 目标值调节 |

---

## 常见问题排查

### STM32 数据未到达 GEC6818

```bash
# 在阿里云 ECS 上验证
mosquitto_sub -t "auaculture_sensor" -h localhost -C 1

# 在 GEC6818 上验证
mosquitto_sub -t "auaculture_sensor" -h 118.178.236.156 -p 1883 -C 1
```

### AI 返回 "系统故障" 或超时

```bash
# PC 端检查
ollama ps                      # 确认模型在运行
ollama logs                    # 查看错误日志
netstat -an | findstr 11434    # 确认监听 0.0.0.0
```

### PWM 无输出

```bash
# GEC6818 上检查 sysfs
ls /sys/class/pwm/pwmchip0/    # 确认 PWM 控制器存在
cat /sys/kernel/debug/pwm      # 查看 PWM 状态（需要 DEBUG_FS）
```

### UI 中文乱码

- 确保 `/simkai.ttf` 字体文件在 GEC6818 根目录
- 确认 `lv_conf.h` 中 `LV_FREETYPE_CACHE_SIZE >= 32`

---

## 任务分工建议（3 人团队）

| 成员 | 板块 | 核心任务 |
|------|------|---------|
| 成员 A | 感知与传输层 | STM32 传感器驱动、ESP8266 AT 指令、MQTT 发布 |
| 成员 B | 边缘计算与控制层 | GEC6818 PID/PWM、LVGL 7 屏界面、数据预处理 |
| 成员 C | 云端与 AI 决策层 | ECS Mosquitto 部署、MQTT 订阅、Ollama API 集成 |

---

## 其他

- [详细知识点与流程文档](系统知识点与流程详解.md)
- [毕业设计论文](论文_智能海洋水产养殖系统.md)
