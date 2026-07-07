#include "mqtt_cloud.h"
#include "../control/auto_control.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>

#define BROKER  "118.178.236.156"
#define PORT    1883
#define USER    "admin"
#define PASS    "123456"
#define TOPIC   "/aquaculture/sensor"

/* 解析 JSON → g_sensor_snapshot */
static void parse_and_update(const char *json)
{
    float t=0, h=0, l=0, d=0, p=0, u=0;
    int aer=0, pump=0, feed=0;
    int n = sscanf(json,
        "{\"water_temp\":%f,\"humidity\":%f,\"light\":%f,"
        "\"dissolved_o2\":%f,\"ph\":%f,\"turbidity\":%f,"
        "\"aerator\":%d,\"pump\":%d,\"feeder\":%d}",
        &t, &h, &l, &d, &p, &u, &aer, &pump, &feed);
    if (n >= 6) {
        g_sensor_snapshot.water_temp   = t;
        g_sensor_snapshot.air_temp     = t;
        g_sensor_snapshot.air_humidity = h;
        g_sensor_snapshot.light_lux    = l;
        g_sensor_snapshot.dissolved_o2 = d;
        g_sensor_snapshot.ph_value     = p;
        g_sensor_snapshot.turbidity_ntu= u;
        g_sensor_snapshot.do_valid     = 1;
        g_sensor_snapshot.temp_valid   = 1;
        g_sensor_snapshot.ph_valid     = 1;
        g_sensor_connected = 1;
    }
}

/* MQTT 订阅线程 */
static void *mqtt_thread(void *arg)
{
    (void)arg;
    char cmd[256];
    snprintf(cmd, sizeof(cmd),
        "mosquitto_sub -t \"%s\" -h \"%s\" -p %d -u \"%s\" -P \"%s\"",
        TOPIC, BROKER, PORT, USER, PASS);

    printf("[MQTT] Subscribe %s @ %s:%d\n", TOPIC, BROKER, PORT);
    fflush(stdout);

    while (1) {
        FILE *fp = popen(cmd, "r");
        if (!fp) { sleep(5); continue; }
        char buf[256];
        while (fgets(buf, sizeof(buf), fp)) {
            /* 去掉换行 */
            buf[strcspn(buf, "\r\n")] = 0;
            if (strlen(buf) > 10) {
                printf("[MQTT] recv: %s\n", buf);
                parse_and_update(buf);
            }
        }
        pclose(fp);
        sleep(1);
    }
    return NULL;
}

void MQTT_Cloud_Init(void)
{
    pthread_t tid;
    pthread_create(&tid, NULL, mqtt_thread, NULL);
    pthread_detach(tid);
}
