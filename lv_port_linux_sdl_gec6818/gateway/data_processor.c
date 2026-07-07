/* ================================================================
 *  data_processor.c — 数据处理模块
 *
 *  功能: 环形缓冲区管理、数据过滤、断网缓存
 * ================================================================ */
#include "gateway.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>

/* 全局变量定义 */
SensorData   g_sensor;
ControlCmd   g_pending_cmd;
GatewayStats g_stats;
volatile int g_running = 1;
RingBuffer   g_ring;

/* ========== 环形缓冲区 ========== */
void ringbuf_init(RingBuffer *rb)
{
    memset(rb, 0, sizeof(RingBuffer));
    pthread_mutex_init(&rb->lock, NULL);
}

void ringbuf_put(RingBuffer *rb, SensorData *d)
{
    pthread_mutex_lock(&rb->lock);
    rb->buf[rb->head] = *d;
    rb->head = (rb->head + 1) % RING_BUF_SIZE;
    if (rb->head == rb->tail) {
        rb->tail = (rb->tail + 1) % RING_BUF_SIZE; /* 覆盖最旧数据 */
        __sync_fetch_and_add(&g_stats.drop_count, 1);
    }
    pthread_mutex_unlock(&rb->lock);
}

int ringbuf_get(RingBuffer *rb, SensorData *d)
{
    pthread_mutex_lock(&rb->lock);
    if (rb->head == rb->tail) {
        pthread_mutex_unlock(&rb->lock);
        return 0; /* 空 */
    }
    *d = rb->buf[rb->tail];
    rb->tail = (rb->tail + 1) % RING_BUF_SIZE;
    pthread_mutex_unlock(&rb->lock);
    return 1;
}

/* ========== 数据过滤: 异常值检测 ========== */
int data_validate(SensorData *d)
{
    /* 物理范围检查 */
    if (d->water_temp < 0.0f || d->water_temp > 50.0f) return 0;
    if (d->dissolved_o2 < 0.0f || d->dissolved_o2 > 20.0f) return 0;
    if (d->ph < 0.0f || d->ph > 14.0f) return 0;
    if (d->turbidity < 0.0f || d->turbidity > 200.0f) return 0;
    return 1;
}

/* ========== 处理线程: 从环形缓冲取数据 → 验证 → 上云 ========== */
void *data_process_thread(void *arg)
{
    (void)arg;
    SensorData sd;

    while (g_running) {
        if (ringbuf_get(&g_ring, &sd)) {
            /* 数据验证 */
            if (!data_validate(&sd)) {
                __sync_fetch_and_add(&g_stats.drop_count, 1);
                continue;
            }

            /* 更新全局变量 (供 LVGL 界面读取) */
            memcpy(&g_sensor, &sd, sizeof(SensorData));

            /* 发布到本地 Broker */
            mqtt_publish_local(&sd);

            /* 上报云端 */
            mqtt_upload_sensor(&sd);
        } else {
            usleep(100000); /* 100ms */
        }
    }
    return NULL;
}
