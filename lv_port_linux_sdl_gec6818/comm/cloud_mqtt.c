/* ================================================================
 *  cloud_mqtt.c — MQTT 数据订阅 + 控制指令 + 鲁棒性
 *  数据流: STM32+ESP8266 → Cloud → mosquitto_sub → g_sensor_snapshot
 * ================================================================ */
#include "cloud_mqtt.h"
#include "../control/auto_control.h"
#include "../control/pid_controller.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <fcntl.h>
#include <math.h>

#define BROKER "118.178.236.156"
#define PORT   1883
#define USER   "admin"
#define PASS   "123456"

/* ---- 订阅传感器数据（独立线程）---- */
static void *sensor_sub_thread(void *arg)
{
    (void)arg;
    char cmd[512];
    snprintf(cmd, sizeof(cmd),
        "mosquitto_sub -t \"auaculture_sensor\" -h \"%s\" "
        "-p %d -u \"%s\" -P \"%s\"",
        BROKER, PORT, USER, PASS);
    printf("[CLOUD] Sub sensor: /aquaculture/sensor\n"); fflush(stdout);

    while (1) {
        FILE *fp = popen(cmd, "r");
        if (!fp) { sleep(3); continue; }
        char buf[512];
        while (fgets(buf, sizeof(buf), fp)) {
            buf[strcspn(buf, "\r\n")] = 0;
            if (strlen(buf) < 10) continue;

            printf("[CLOUD] DATA: %s\n", buf);
            fflush(stdout);

            float t=0,h=0,l=0,d=0,p=0,u=0;
            int n = sscanf(buf,
                "{\"temp\":%f,\"humi\":%f,\"light\":%f,"
                "\"do\":%f,\"ph\":%f,\"turb\":%f",
                &t,&h,&l,&d,&p,&u);
            if (n >= 3) {  /* 至少有温度 */
                g_sensor_snapshot.water_temp   = t;
                g_sensor_snapshot.air_temp     = t;
                g_sensor_snapshot.air_humidity = h;
                g_sensor_snapshot.light_lux    = l;
                g_sensor_snapshot.dissolved_o2 = d;
                g_sensor_snapshot.ph_value     = p;
                g_sensor_snapshot.turbidity_ntu= u;
                g_sensor_snapshot.do_valid     = (d>0.1f) ? 1 : 0;
                g_sensor_snapshot.temp_valid   = (t>0.0f) ? 1 : 0;
                g_sensor_snapshot.ph_valid     = (p>0.0f) ? 1 : 0;
                g_sensor_connected = 1;  /* MQTT 有数据 → 停止模拟 */
            }
        }
        pclose(fp);
        sleep(1);
    }
    return NULL;
}

/* ---- 传感器故障检测 ---- */
int CloudMQTT_CheckFault(void)
{
    static float last_d=0, last_t=0; static int stale=0;
    float d = g_sensor_snapshot.dissolved_o2;
    float t = g_sensor_snapshot.water_temp;
    if ((d>0||t>0) && (d<0.1f||d>20.0f||t<0.0f||t>50.0f)) return 1;
    if (fabsf(d-last_d)<0.01f && fabsf(t-last_t)<0.01f) stale++;
    else { stale=0; last_d=d; last_t=t; }
    return (stale>60) ? 2 : 0;
}

/* ---- 订阅控制指令 ---- */
static void *ctrl_sub_thread(void *arg)
{
    (void)arg;
    char cmd[512];
    snprintf(cmd, sizeof(cmd),
        "mosquitto_sub -t \"auaculture_control\" -h \"%s\" "
        "-p %d -u \"%s\" -P \"%s\" -C 1", BROKER, PORT, USER, PASS);
    printf("[CLOUD] Sub ctrl: /aquaculture/control\n"); fflush(stdout);

    while (1) {
        FILE *fp = popen(cmd, "r");
        if (!fp) { sleep(5); continue; }
        char buf[256];
        while (fgets(buf, sizeof(buf), fp)) {
            buf[strcspn(buf,"\r\n")]=0;
            printf("[CLOUD] CMD: %s\n", buf);
            char dev[32]={0}; int on=0; float v;
            if (sscanf(buf,"{\"dev\":\"%[^\"]\",\"on\":%d}",dev,&on)>=2) {
                if(strcmp(dev,"aerator")==0) PID_SetMode(0,on);
                else if(strcmp(dev,"pump")==0) PID_SetMode(2,on);
                else if(strcmp(dev,"feeder")==0) PID_SetMode(1,on);
            }
            if (sscanf(buf,"{\"do_target\":%f}",&v)>=1) g_thresholds.do_target=v;
            if (sscanf(buf,"{\"temp_target\":%f}",&v)>=1) g_thresholds.temp_target=v;
        }
        pclose(fp);
    }
    return NULL;
}

void CloudMQTT_Init(void)
{
    pthread_t t1,t2,t3;
    pthread_create(&t1, NULL, sensor_sub_thread, NULL); pthread_detach(t1);
    pthread_create(&t2, NULL, ctrl_sub_thread, NULL);  pthread_detach(t2);
    /* 看门狗 */
    int fd = open("/dev/watchdog", O_RDWR);
    if (fd >= 0) { printf("[WD] enabled\n"); close(fd); }
    printf("[CLOUD] OK (Broker %s:%d)\n", BROKER, PORT);
}
