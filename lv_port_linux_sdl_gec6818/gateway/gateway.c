/* ================================================================
 *  gateway.c — GEC6818 多协议传输网关 主程序
 *
 *  编译: make -f gateway_makefile
 *  运行: ./gateway
 *
 *  架构:
 *    STM32(UART JSON) → GEC6818 → 本地 Mosquitto → 阿里云 ECS
 *                                    ↓
 *                              控制指令 (← Mosquitto订阅)
 * ================================================================ */
#include "gateway.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <pthread.h>
#include <time.h>

/* 外部函数声明 */
extern int  serial_init(const char *dev, int baud);
extern void serial_close(void);
extern void *serial_rx_thread(void *arg);
extern void *data_process_thread(void *arg);
extern void *mqtt_subscribe_thread(void *arg);
extern void *mqtt_heartbeat_thread(void *arg);
extern int  mqtt_publish_local(SensorData *sd);
extern int  mqtt_upload_sensor(SensorData *sd);
extern void ringbuf_init(RingBuffer *rb);

/* 信号处理 */
static void sig_handler(int s)
{
    (void)s;
    printf("\n[GW] Shutting down...\n");
    g_running = 0;
}

/* 打印统计信息 */
static void print_stats(void)
{
    printf("\n========== Gateway Stats ==========\n");
    printf("  RX: %u  TX: %u  ERR: %u  DROP: %u\n",
           g_stats.rx_count, g_stats.tx_count,
           g_stats.err_count, g_stats.drop_count);
    printf("  UART: %s  MQTT: %s\n",
           g_stats.uart_ok ? "OK" : "FAIL",
           g_stats.mqtt_ok  ? "OK" : "FAIL");
    printf("  Avg Latency: %.1f ms\n", g_stats.avg_latency_ms);
    printf("====================================\n\n");
}

int main(int argc, char *argv[])
{
    pthread_t tid_uart, tid_proc, tid_sub, tid_hb;

    printf("\n");
    printf("╔══════════════════════════════════╗\n");
    printf("║  GEC6818 Gateway v%s          ║\n", GATEWAY_VERSION);
    printf("║  STM32 → UART → Mosquitto → Cloud║\n");
    printf("╚══════════════════════════════════╝\n\n");

    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);

    /* 初始化 */
    ringbuf_init(&g_ring);
    memset(&g_sensor, 0, sizeof(g_sensor));
    memset(&g_pending_cmd, 0, sizeof(g_pending_cmd));
    memset(&g_stats, 0, sizeof(g_stats));

    /* 1. 打开串口 */
    if (serial_init(SENSOR_UART, SENSOR_BAUD) == 0) {
        g_stats.uart_ok = 1;
        pthread_create(&tid_uart, NULL, serial_rx_thread, NULL);
    } else {
        printf("[GW] UART not available, using local MQTT only\n");
    }

    /* 2. 启动数据处理线程 */
    pthread_create(&tid_proc, NULL, data_process_thread, NULL);

    /* 3. 启动 MQTT 订阅 (云端控制指令) */
    pthread_create(&tid_sub, NULL, mqtt_subscribe_thread, NULL);

    /* 4. 启动心跳 */
    pthread_create(&tid_hb, NULL, mqtt_heartbeat_thread, NULL);

    printf("[GW] Gateway started\n");
    printf("[GW] Local MQTT: %s:%d\n", LOCAL_BROKER_IP, LOCAL_BROKER_PORT);
    printf("[GW] Cloud MQTT: %s:%d\n", CLOUD_BROKER_IP, CLOUD_BROKER_PORT);

    /* 主循环: 每 30 秒打印状态 */
    while (g_running) {
        sleep(30);
        if (g_running) print_stats();
    }

    /* 清理 */
    pthread_join(tid_uart, NULL);
    pthread_join(tid_proc, NULL);
    serial_close();
    printf("[GW] Gateway stopped\n");
    return 0;
}
