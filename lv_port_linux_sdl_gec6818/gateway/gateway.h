/* ================================================================
 *  gateway.h — GEC6818 多协议传输网关 (核心头文件)
 *
 *  架构: STM32(UART) → GEC6818(网关) → 阿里云IoT(MQTT)
 *        ESP8266 在 STM32 端，GEC6818 通过 UART 接收 JSON
 *        GEC6818 本地运行 Mosquitto → mosquitto_pub 上报云
 * ================================================================ */
#ifndef __GATEWAY_H
#define __GATEWAY_H

#include <stdint.h>
#include <pthread.h>

/* ========== 配置 ========== */
#define GATEWAY_VERSION  "1.0.0"

/* 串口 (STM32 → GEC6818) */
#define SENSOR_UART      "/dev/ttySAC2"
#define SENSOR_BAUD      115200

/* 本地 Mosquitto Broker */
#define LOCAL_BROKER_IP   "127.0.0.1"
#define LOCAL_BROKER_PORT 1883

/* 阿里云 ECS MQTT Broker */
#define CLOUD_BROKER_IP   "118.178.236.156"
#define CLOUD_BROKER_PORT 1883
#define CLOUD_USER        "admin"
#define CLOUD_PASS        "123456"

/* MQTT Topics */
#define TOPIC_SENSOR_RAW    "/stm32/data"          /* 本地：STM32原始数据 */
#define TOPIC_SENSOR_CLOUD  "/aquaculture/sensor"   /* 云：传感器上云 */
#define TOPIC_CONTROL_LOCAL "/stm32/control"        /* 本地：控制指令 */
#define TOPIC_CONTROL_CLOUD "/aquaculture/control"  /* 云：远程控制 */
#define TOPIC_STATUS        "/aquaculture/status"   /* 云：心跳 */

/* 环形缓冲区大小 */
#define RING_BUF_SIZE    32

/* ========== 数据结构 ========== */

/* 传感器数据 */
typedef struct {
    float  water_temp;
    float  humidity;
    float  light;
    float  dissolved_o2;
    float  ph;
    float  turbidity;
    int    sensor_status;
    char   timestamp[32];
} SensorData;

/* 控制指令 */
typedef struct {
    char   cmd[64];
    char   params[256];
} ControlCmd;

/* 环形缓冲区 */
typedef struct {
    SensorData buf[RING_BUF_SIZE];
    int        head;
    int        tail;
    pthread_mutex_t lock;
} RingBuffer;

/* 网关统计 */
typedef struct {
    uint32_t rx_count;       /* 接收计数 */
    uint32_t tx_count;       /* 发送计数 */
    uint32_t err_count;      /* 错误计数 */
    uint32_t drop_count;     /* 丢包计数 */
    int      uart_ok;        /* 串口状态 */
    int      mqtt_ok;        /* MQTT状态 */
    int      wifi_ok;        /* WiFi状态 */
    double   avg_latency_ms; /* 平均延迟 */
} GatewayStats;

/* ========== 全局变量(外部可见) ========== */
extern SensorData    g_sensor;
extern ControlCmd    g_pending_cmd;
extern GatewayStats  g_stats;
extern volatile int  g_running;

/* ========== 函数声明 ========== */
void  gateway_init(void);
void  gateway_run(void);
void  gateway_stop(void);

void  ringbuf_put(RingBuffer *rb, SensorData *d);
int   ringbuf_get(RingBuffer *rb, SensorData *d);

#endif
