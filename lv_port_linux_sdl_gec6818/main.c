#include "lvgl/lvgl.h"
#include "lvgl/demos/lv_demos.h"
#include "ui/ui.h"
#include "control/auto_control.h"
#include "control/pid_controller.h"
#include "control/pwm_driver.h"
#include <unistd.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <sys/time.h>

static void sig_handler(int s){(void)s;AutoControl_Stop();PWM_DeinitAll();exit(0);}

static void lv_linux_disp_init(void)
{
    lv_display_t *disp = lv_linux_fbdev_create();
    lv_linux_fbdev_set_file(disp, "/dev/fb0");
}

/* ---- MQTT 订阅线程：从云端拿数据更新 g_sensor_snapshot ---- */
static void *mqtt_sub_thread(void *arg)
{
    (void)arg;
    char cmd[512];
    snprintf(cmd, sizeof(cmd),
        "mosquitto_sub -t \"auaculture_sensor\" "
        "-h \"118.178.236.156\" -p 1883 -C 1");
    printf("[MQTT] Sub start: auaculture_sensor\n"); fflush(stdout);

    double avg_latency = 0;
    int    lat_count   = 0;

    while (1) {
        struct timeval t1, t2;
        gettimeofday(&t1, NULL);

        FILE *fp = popen(cmd, "r");
        if (!fp) { printf("[MQTT] popen fail\n"); fflush(stdout); sleep(3); continue; }
        char buf[512];
        if (fgets(buf, sizeof(buf), fp)) {
            buf[strcspn(buf, "\r\n")] = 0;

            gettimeofday(&t2, NULL);
            double lat = (t2.tv_sec - t1.tv_sec) * 1000.0 + (t2.tv_usec - t1.tv_usec) / 1000.0;
            avg_latency = avg_latency * 0.9 + lat * 0.1;
            lat_count++;
            if (lat_count % 10 == 0)
                printf("[MQTT] latency: %.1f ms\n", avg_latency);
            fflush(stdout);

            if (strlen(buf) < 10) { pclose(fp); continue; }

            float t=0,h=0,l=0,d=0,p=0,u=0;
            int n = sscanf(buf,
                "{\"temp\":%f,\"humi\":%f,\"light\":%f,"
                "\"do\":%f,\"ph\":%f,\"turb\":%f",
                &t,&h,&l,&d,&p,&u);
            if (n >= 3) {
                g_sensor_snapshot.water_temp   = t;
                g_sensor_snapshot.air_temp     = t;
                g_sensor_snapshot.air_humidity = h;
                g_sensor_snapshot.light_lux    = l;
                g_sensor_snapshot.dissolved_o2 = d;
                g_sensor_snapshot.ph_value     = p;
                g_sensor_snapshot.turbidity_ntu= u;
                g_sensor_snapshot.do_valid     = (d > 0.1f) ? 1 : 0;
                g_sensor_snapshot.temp_valid   = (t > 0.0f) ? 1 : 0;
                g_sensor_snapshot.ph_valid     = (p > 0.0f) ? 1 : 0;
                if (!g_sensor_connected) {
                    g_sensor_connected = 1;
                    printf("[MQTT] 真实数据已连接\n"); fflush(stdout);
                }
                printf("[MQTT] T=%.1f H=%.1f L=%.0f DO=%.2f pH=%.2f Turb=%.1f\n",
                       t, h, l, d, p, u); fflush(stdout);
            }
        } else {
            printf("[MQTT] no data\n"); fflush(stdout);
        }
        pclose(fp);
    }
    return NULL;
}

int main(void)
{
    signal(SIGINT,sig_handler);signal(SIGTERM,sig_handler);
    lv_init();
    lv_linux_disp_init();

    lv_indev_t *touch = lv_evdev_create(LV_INDEV_TYPE_POINTER, "/dev/input/event0");
    lv_evdev_set_calibration(touch, 0, 0, 1024, 600);

    ui_init();
    AutoControl_Init();
    /* 不用串口直连，数据从 MQTT 云订阅 */
    SENSOR_UART_INIT();

    /* MQTT 订阅线程 */
    { pthread_t t; pthread_create(&t, NULL, mqtt_sub_thread, NULL); pthread_detach(t); }

    pthread_t ct; pthread_create(&ct,NULL,AutoControl_Loop,NULL); pthread_detach(ct);
    lv_timer_create(ui_S2_update_timer, 500, NULL);

    while(1) { lv_timer_handler(); usleep(5000); }
    return 0;
}
