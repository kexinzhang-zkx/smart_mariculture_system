# Smart Marine Aquaculture System

> GEC6818 + STM32F103 + ESP8266 + Alibaba Cloud + DeepSeek-R1  

## Overview

A complete smart marine aquaculture system implementing a full closed-loop chain from sensor acquisition, multi-protocol transmission, edge computing, PID control, and HMI to cloud-based AI decision-making.

### Core Features

| Module | Function | Implementation |
|--------|----------|---------------|
| Multi-source Sensing | Temperature, humidity, light, DO, pH, turbidity | DHT11 + ADC + simulation |
| Multi-protocol Gateway | WiFi → MQTT → Cloud | ESP8266 AT commands + Mosquitto |
| Edge Data Preprocessing | Dedup, anomaly filter, quality scoring, persistence | Sliding window + threshold detection |
| PID+PWM Closed-loop | Aerator, water pump, feeder motor | Positional PID + sysfs PWM |
| HMI | 7-screen LVGL touch interface | LVGL 9.1 + FreeType |
| Cloud AI Decision | Evaluation, diagnosis, suggestion, maintenance | Ollama + DeepSeek-R1:1.5B |

---

## System Architecture

```
┌─────────────────────┐         MQTT/JSON          ┌──────────────────────────┐
│   STM32F103          │ ──────────────────────────→ │   Alibaba Cloud ECS      │
│   (Sensor Node)      │   topic: auaculture_sensor  │   Mosquitto Broker       │
│                     │                             │   118.178.236.156:1883   │
│  DHT11 → PA15       │                             └───────────┬──────────────┘
│  Photoresistor → PA5│                                         │
│  DO/pH/Turb (sim)   │                               mosquitto_sub
│  ESP8266 → USART1   │                                         ↓
└─────────────────────┘                             ┌──────────────────────────┐
                                                     │   GEC6818 ARM Linux      │
                                                     │   (Edge Computing)        │
                                                     │                          │
                                                     │  ┌────────────────────┐  │
                                                     │  │ Data Preprocessing │  │
                                                     │  │ Dedup/Anomaly/Tags │  │
                                                     │  └─────────┬──────────┘  │
                                                     │            ↓             │
                                                     │  ┌────────────────────┐  │
                           HTTP POST                 │  │ PID Control        │  │
                           /api/generate             │  │ Aerator/Pump/Feeder│  │
  ┌──────────────────┐ ←─────────────────────────── │  │ PWM via sysfs      │  │
  │  PC (Ollama)     │                               │  └─────────┬──────────┘  │
  │  DeepSeek-R1:1.5B│                               │            ↓             │
  │  192.168.29.23   │                               │  ┌────────────────────┐  │
  │  :11434          │                               │  │ LVGL 7-Screen UI   │  │
  └──────────────────┘                               │  │ PID/AI/Data/Sensor │  │
                                                     │  └────────────────────┘  │
                                                     └──────────────────────────┘
```

---

## Hardware

| Device | Model | Function |
|--------|-------|----------|
| ARM Linux Board | GEC6818 (S5P6818) | Edge computing core |
| MCU | STM32F103C8T6 | Sensor data acquisition |
| WiFi Module | ESP8266 (AT firmware) | MQTT wireless transmission |
| Temp/Humidity | DHT11 | Air temperature & humidity |
| Photoresistor | GL5528 | Light intensity |
| Touchscreen | 7" 800×480 | LVGL HMI |

---

## Software Stack

| Layer | Technology | Version |
|-------|-----------|---------|
| GUI | LVGL | 9.1.0 |
| Font Engine | FreeType | 2.x |
| AI Model | DeepSeek-R1 | 1.5B (Ollama) |
| MQTT Broker | Mosquitto | 2.x |
| STM32 Library | SPL | 3.5 |
| Toolchain | GCC (arm-linux-gcc / arm-none-eabi-gcc) | — |

---

## Directory Structure

```
project/
├── lv_port_linux_sdl_gec6818/       # GEC6818 main project
│   ├── main.c                        # Entry: LVGL + MQTT subscribe + control thread
│   ├── Makefile                      # Cross-compilation script
│   ├── control/
│   │   ├── auto_control.c/h          # Control engine: sense→judge→PID→PWM
│   │   ├── pid_controller.c/h        # PID algorithm: positional + anti-windup
│   │   └── pwm_driver.c/h            # PWM driver: Linux sysfs interface
│   ├── ui/
│   │   ├── ui.c/h                    # LVGL entry + screen navigation events
│   │   ├── ui_events.c/h             # All business logic event handlers
│   │   └── screens/
│   │       ├── ui_Screen1.c          # Login / Register
│   │       ├── ui_Screen2.c          # PID Control Panel
│   │       ├── ui_Screen4.c          # AI Decision Assistant
│   │       ├── ui_Screen5.c          # Data Preprocessing & Analytics
│   │       ├── ui_Screen6.c          # Real-time Sensor Monitor + Chart
│   │       └── ui_Screen7.c          # Navigation Menu
│   └── comm/
│       └── cloud_mqtt.c/h            # MQTT module (deprecated, moved to main.c)
│
├── 01/                               # STM32 project
│   ├── User/main.c                   # Main: DHT11 + ADC + ESP8266 + MQTT
│   ├── User/stm32f10x_it.c           # Interrupt handlers
│   ├── system/
│   │   ├── dht11.c/h                 # DHT11 One-Wire driver
│   │   ├── adc_light.c/h             # ADC photoresistor driver
│   │   ├── sensor.c/h                # Sensor aggregation + JSON formatting
│   │   ├── esp8266.c/h               # ESP8266 AT command driver
│   │   ├── mqtt.c/h                  # Raw MQTT packet builder (reserved)
│   │   ├── dwt_delay.c/h             # DWT microsecond delay
│   │   └── Delay.c/h                 # Millisecond delay
│   ├── Library/                      # STM32 Standard Peripheral Library
│   └── Start/                        # Startup files + system clock config
│
├── STM32发布数据到MQTT完整代码V1(1)/  # Reference code
├── 论文_智能海洋水产养殖系统.md        # Thesis (Chinese)
├── 系统知识点与流程详解.md             # Knowledge points & flow (Chinese)
├── README.md                          # This file (Chinese)
└── README_EN.md                       # This file (English)
```

---

## Quick Start

### 1. STM32 Deployment

**Requirements:**
- Keil MDK 5
- J-Link / ST-Link programmer
- `01/` is a complete Keil project

**Steps:**
1. Open `01/` in Keil MDK
2. Verify `WIFI_SSID` / `WIFI_PWD` configuration
3. Build → Flash to STM32F103

**Key configuration** (`01/User/main.c`):
```c
#define WIFI_SSID       "sunbao"
#define WIFI_PWD        "12345678"
#define MQTT_BROKER_IP  "118.178.236.156"
#define MQTT_TOPIC      "auaculture_sensor"
```

### 2. Alibaba Cloud ECS Setup

```bash
# Install Mosquitto
sudo apt install mosquitto mosquitto-clients -y

# Configure anonymous access
sudo tee -a /etc/mosquitto/mosquitto.conf <<EOF
listener 1883 0.0.0.0
allow_anonymous true
EOF

# Start
sudo systemctl restart mosquitto

# Verify
mosquitto_sub -t "auaculture_sensor" -h localhost -C 1
```

### 3. GEC6818 Deployment

```bash
# Cross-compile
cd lv_port_linux_sdl_gec6818/
make clean && make

# Deploy to GEC6818
scp ./demo root@<gec6818_ip>:/root/

# Run on GEC6818
./demo
```

### 4. AI Server Setup (PC)

```bash
# Install Ollama
curl -fsSL https://ollama.com/install.sh | sh

# Pull model
ollama pull deepseek-r1:1.5b

# Start (listen on all interfaces)
# Windows: set OLLAMA_HOST=0.0.0.0 && ollama serve
OLLAMA_HOST=0.0.0.0 ollama serve

# Verify
curl -X POST http://localhost:11434/api/generate \
  -H "Content-Type: application/json" \
  -d '{"model":"deepseek-r1:1.5b","prompt":"hello","stream":false}'
```

### 5. Startup Order

```
① PC (Ollama)           → Start deepseek-r1:1.5b
② Alibaba Cloud ECS     → Start Mosquitto
③ STM32 + ESP8266       → Power on, auto-connect WiFi & publish
④ GEC6818               → Run ./demo, auto-subscribe MQTT
```

---

## LVGL Screen Navigation

```
Screen1 (Login / Register)
    │
    ↓ Login success
Screen7 (Navigation Menu)
    ├──→ Screen4 (AI Decision Assistant)
    │      ┌─ Evaluate: inject DO/T/pH/Turb/Lux → score 1-10
    │      ├─ Diagnose: inject device status → detect faults
    │      ├─ Suggest: inject water quality → management advice
    │      └─ Maintain: inject runtime → maintenance guidance
    │
    ├──→ Screen5 (Data Preprocessing)
    │      ┌─ Container6: Real-time data quality (OK/DUP/ERR/LOW/HIGH)
    │      ├─ Container8: 24h statistics (max/min/avg/trend)
    │      ├─ Container9: Trend prediction
    │      └─ Container10: Storage status (eMMC/records/space)
    │
    ├──→ Screen2 (PID Control Panel)
    │      ┌─ Container1: Device status (3 switches + duty cycle)
    │      ├─ Container4: PID parameter tuning (3 devices × 6 buttons)
    │      └─ Container5: Real-time data + target value adjustment
    │
    └──→ Screen6 (Sensor Monitor)
           ┌─ Container3: 6-channel sensor values
           ├─ Real-time line chart (6 series, 30 data points)
           └─ Color legend
```

---

## PID Control Parameters

| Device | Kp | Ki | Kd | Target | Cycle | Mode |
|--------|----|----|-----|--------|-------|------|
| Aerator | 1.5 | 0.3 | 0.05 | DO→5.0 mg/L | 200ms | PID closed-loop |
| Water Pump | 1.0 | 0.2 | 0 | T→25.0°C | 200ms | PID closed-loop |
| Feeder Motor | 0.8 | 0.1 | 0 | 60s/cycle | 200ms | Timer-based |

**PID Features:**
- Positional PID + integral separation (disable integral when error > 30%)
- Dead zone 0.5% (avoid micro-adjustment wear)
- Output clamping (0~100%) + anti-windup
- Online tuning via Screen2 (Kp±0.1, Ki±0.05, Kd±0.01)

---

## PWM Channel Mapping

| Device | PWM Channel | sysfs Path |
|--------|------------|------------|
| Aerator | PWM0 | `/sys/class/pwm/pwmchip0/pwm0/` |
| Feeder Motor | PWM1 | `/sys/class/pwm/pwmchip0/pwm1/` |
| Water Pump | PWM2 | `/sys/class/pwm/pwmchip0/pwm2/` |

- PWM Frequency: 1kHz (`period = 1,000,000 ns`)
- Duty Cycle = `duty_cycle / period × 100%`

---

## MQTT Data Format

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

**Field Reference:**

| Field | Meaning | Unit | Source |
|-------|---------|------|--------|
| temp | Air temperature | °C | DHT11 |
| humi | Air humidity | %RH | DHT11 |
| light | Light intensity | lux | ADC photoresistor |
| do | Dissolved oxygen | mg/L | Software simulation |
| ph | pH value | — | Software simulation |
| turb | Turbidity | NTU | Software simulation |

---

## Data Preprocessing Rules

### Quality Tag Priority

```
Sensor validity > Physical range (ERR) > Duplicate (DUP) > Threshold (LOW/HIGH) > OK
```

### Physical Range Constraints

| Parameter | Range | Violation Judgement |
|-----------|-------|-------------------|
| DO | 0.1 ~ 20.0 mg/L | Sensor fault |
| Water Temp | 0.0 ~ 50.0 °C | Sensor fault |
| pH | 0.0 ~ 14.0 | Sensor fault |
| Turbidity | 0.0 ~ 200.0 NTU | Sensor fault |

### Aquaculture Thresholds

| Parameter | Low | High | Target |
|-----------|-----|------|--------|
| DO | 2.0 mg/L | 9.0 mg/L | 5.0 mg/L |
| Water Temp | 15.0 °C | 35.0 °C | 25.0 °C |

---

## AI Prompt Design

| Button | Injected Data | Task |
|--------|--------------|------|
| Evaluate | DO, T, pH, Turb, Lux | Rate water quality, ≤30 words |
| Diagnose | Aerator%, Pump%, Feeder state, DO, T | Any faults? ≤20 words |
| Suggest | DO, T, pH, Turb | Management advice, ≤30 words |
| Maintain | 3-device runtime + duty cycle | Maintenance tips, ≤30 words |

---

## Data Persistence

- **File path:** `./sensor.log`
- **Format:** CSV (comma-separated values)
- **Write interval:** Every 30 seconds
- **Sample record:**
  ```
  26.3,67.4,5.2,7.90,42.6,1091
  ```
- **Storage location:** GEC6818 eMMC

---

## Debug Log Tags

| Tag | Source | Meaning |
|-----|--------|---------|
| `[MQTT]` | main.c | MQTT subscribe status / latency / received data |
| `[CTRL]` | auto_control.c | Control loop: DO value / duty cycle / anomalies |
| `[SYS]` | ui_events.c | System summary (every 60s) |
| `[AI]` | ui_events.c | AI request / response status |
| `[PWM]` | pwm_driver.c | PWM initialization status |
| `[LOGIN]` | ui_events.c | Login / register operations |
| `[TARGET]` | ui_events.c | Screen2 target value adjustments |

---

## Troubleshooting

### STM32 data not reaching GEC6818

```bash
# Verify on Alibaba Cloud ECS
mosquitto_sub -t "auaculture_sensor" -h localhost -C 1

# Verify on GEC6818
mosquitto_sub -t "auaculture_sensor" -h 118.178.236.156 -p 1883 -C 1
```

### AI returns "System Error" or times out

```bash
# Check on PC
ollama ps                       # Confirm model is running
ollama logs                     # Check error logs
netstat -an | findstr 11434     # Confirm listening on 0.0.0.0 (Windows)
# or
ss -tlnp | grep 11434           # (Linux)
```

### PWM has no output

```bash
# Check on GEC6818
ls /sys/class/pwm/pwmchip0/     # Confirm PWM controller exists
cat /sys/kernel/debug/pwm       # View PWM status (requires DEBUG_FS)
```

### UI shows garbled Chinese characters

- Ensure `/simkai.ttf` font file exists in GEC6818 root directory
- Confirm `lv_conf.h` has `LV_FREETYPE_CACHE_SIZE >= 32`

---

## Task Breakdown (3-person team)

| Member | Layer | Core Tasks |
|--------|-------|-----------|
| Member A | Perception & Transport | STM32 sensor drivers, ESP8266 AT commands, MQTT publish |
| Member B | Edge Computing & Control | GEC6818 PID/PWM, LVGL 7-screen UI, data preprocessing |
| Member C | Cloud & AI | ECS Mosquitto deployment, MQTT subscribe, Ollama API integration |

---

## References

- [Detailed Knowledge Points & Flow (Chinese)](系统知识点与流程详解.md)
- [Graduation Thesis (Chinese)](论文_智能海洋水产养殖系统.md)
- [Project README (Chinese)](README.md)
