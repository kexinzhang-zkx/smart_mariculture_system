/* ================================================================
 *  mqtt_manager.c — MQTT 管理模块
 *
 *  方案: 使用 mosquitto_pub/mosquitto_sub 工具（board已有）
 *        封装为 popen 调用,支持发布/订阅/心跳
 * ================================================================ */
#include "gateway.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

/* 发布消息到指定 Broker/Topic */
int mqtt_publish(const char *broker, int port,
    const char *user, const char *pass,
    const char *topic, const char *payload)
{
    char cmd[2048];
    struct timeval t1, t2;

    gettimeofday(&t1, NULL);

    snprintf(cmd, sizeof(cmd),
        "mosquitto_pub -t \"%s\" -h \"%s\" -p %d "
        "-u \"%s\" -P \"%s\" -m '%s' -q 0 2>/dev/null",
        topic, broker, port,
        user ? user : "", pass ? pass : "",
        payload);

    int ret = system(cmd);

    gettimeofday(&t2, NULL);
    double lat = (t2.tv_sec - t1.tv_sec) * 1000.0 +
                 (t2.tv_usec - t1.tv_usec) / 1000.0;

    /* 更新延迟统计 */
    g_stats.avg_latency_ms = (g_stats.avg_latency_ms * 0.9) + (lat * 0.1);

    if (ret == 0) {
        __sync_fetch_and_add(&g_stats.tx_count, 1);
        return 0;
    }
    __sync_fetch_and_add(&g_stats.err_count, 1);
    return -1;
}

/* 发布传感器数据到云 */
void mqtt_upload_sensor(SensorData *sd)
{
    char json[512];
    snprintf(json, sizeof(json),
        "{\"water_temp\":%.1f,\"humidity\":%.1f,\"light\":%.0f,"
        "\"dissolved_o2\":%.2f,\"ph\":%.2f,\"turbidity\":%.1f,"
        "\"status\":%d,\"time\":\"%s\"}",
        sd->water_temp, sd->humidity, sd->light,
        sd->dissolved_o2, sd->ph, sd->turbidity,
        sd->sensor_status, sd->timestamp);

    mqtt_publish(CLOUD_BROKER_IP, CLOUD_BROKER_PORT,
        CLOUD_USER, CLOUD_PASS, TOPIC_SENSOR_CLOUD, json);
}

/* 发布到本地 Broker (供其他模块订阅) */
void mqtt_publish_local(SensorData *sd)
{
    char json[512];
    snprintf(json, sizeof(json),
        "{\"node\":\"01\",\"temp\":%.1f,\"humidity\":%.1f,"
        "\"light\":%.0f,\"do\":%.2f,\"ph\":%.2f,\"turb\":%.1f}",
        sd->water_temp, sd->humidity, sd->light,
        sd->dissolved_o2, sd->ph, sd->turbidity);

    mqtt_publish(LOCAL_BROKER_IP, LOCAL_BROKER_PORT,
        NULL, NULL, TOPIC_SENSOR_RAW, json);
}

/* 订阅云端控制指令 (独立线程) */
void *mqtt_subscribe_thread(void *arg)
{
    (void)arg;
    char cmd[512];

    snprintf(cmd, sizeof(cmd),
        "mosquitto_sub -t \"%s\" -h \"%s\" -p %d "
        "-u \"%s\" -P \"%s\"",
        TOPIC_CONTROL_CLOUD, CLOUD_BROKER_IP, CLOUD_BROKER_PORT,
        CLOUD_USER, CLOUD_PASS);

    printf("[GW-MQTT] Subscribe: %s\n", TOPIC_CONTROL_CLOUD);

    while (g_running) {
        FILE *fp = popen(cmd, "r");
        if (!fp) { sleep(3); continue; }

        char buf[256];
        while (g_running && fgets(buf, sizeof(buf), fp)) {
            buf[strcspn(buf, "\r\n")] = 0;
            if (strlen(buf) > 4) {
                printf("[GW-MQTT] CMD: %s\n", buf);
                /* 解析并存储控制指令 */
                sscanf(buf, "{\"cmd\":\"%[^\"]\",\"params\":\"%[^\"]\"}",
                       g_pending_cmd.cmd, g_pending_cmd.params);
                /* 转发到本地控制 Topic */
                mqtt_publish(LOCAL_BROKER_IP, LOCAL_BROKER_PORT,
                    NULL, NULL, TOPIC_CONTROL_LOCAL, buf);
            }
        }
        pclose(fp);
        sleep(1);
    }
    return NULL;
}

/* 心跳: 每 60 秒一次 */
void *mqtt_heartbeat_thread(void *arg)
{
    (void)arg;
    while (g_running) {
        sleep(60);
        char hb[128];
        snprintf(hb, sizeof(hb),
            "{\"status\":\"online\",\"uptime\":%ld,"
            "\"rx\":%u,\"tx\":%u,\"err\":%u}",
            time(NULL), g_stats.rx_count,
            g_stats.tx_count, g_stats.err_count);
        mqtt_publish(CLOUD_BROKER_IP, CLOUD_BROKER_PORT,
            CLOUD_USER, CLOUD_PASS, TOPIC_STATUS, hb);
    }
    return NULL;
}
