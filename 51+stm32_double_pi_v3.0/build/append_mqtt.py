f = '/home/huangsimin09/lv_port_linux_sdl_gec6818/ui/ui_events.c'
code = '''

#include <pthread.h>
#include <unistd.h>
static pthread_t mqtt_thread;
static volatile int mqtt_running = 0;
static volatile float g_mqtt_temp = 0, g_mqtt_humi = 0, g_mqtt_light = 0;
static volatile float g_mqtt_do = 0, g_mqtt_ph = 0, g_mqtt_turb = 0;
static volatile int g_mqtt_updated = 0;

static void *mqtt_subscribe_thread(void *arg)
{
    while (mqtt_running) {
        FILE *fp = popen("mosquitto_sub -t \\"gec6818\\" -h \\"116.62.216.186\\" -p 1883 -C 1", "r");
        if (!fp) { sleep(3); continue; }
        char buf[512] = {0};
        if (fgets(buf, sizeof(buf), fp)) {
            buf[strcspn(buf, "\\r\\n")] = 0;
            char *p;
            if ((p = strstr(buf, "\\"temp\\":")))  g_mqtt_temp  = atof(p + 7);
            if ((p = strstr(buf, "\\"humi\\":")))  g_mqtt_humi  = atof(p + 7);
            if ((p = strstr(buf, "\\"light\\":"))) g_mqtt_light = atof(p + 8);
            if ((p = strstr(buf, "\\"do\\":")))    g_mqtt_do    = atof(p + 5);
            if ((p = strstr(buf, "\\"ph\\":")))    g_mqtt_ph    = atof(p + 5);
            if ((p = strstr(buf, "\\"turb\\":")))  g_mqtt_turb  = atof(p + 7);
            g_mqtt_updated = 1;
        }
        pclose(fp);
        sleep(2);
    }
    return NULL;
}

void mqtt_subscribe_start(void)
{
    if (mqtt_running) return;
    mqtt_running = 1;
    pthread_create(&mqtt_thread, NULL, mqtt_subscribe_thread, NULL);
}

static void update_sensor_labels(lv_timer_t *timer)
{
    if (!g_mqtt_updated) return;
    g_mqtt_updated = 0;
    char buf[32];
    if (ui_SensorTemp)  { snprintf(buf, sizeof(buf), "%.1f C", g_mqtt_temp);  lv_label_set_text(ui_SensorTemp,  buf); }
    if (ui_SensorHumi)  { snprintf(buf, sizeof(buf), "%.1f %%", g_mqtt_humi);  lv_label_set_text(ui_SensorHumi,  buf); }
    if (ui_SensorLight) { snprintf(buf, sizeof(buf), "%.0f",   g_mqtt_light); lv_label_set_text(ui_SensorLight, buf); }
    if (ui_SensorDO)    { snprintf(buf, sizeof(buf), "%.2f",   g_mqtt_do);    lv_label_set_text(ui_SensorDO,    buf); }
    if (ui_SensorPH)    { snprintf(buf, sizeof(buf), "%.2f",   g_mqtt_ph);    lv_label_set_text(ui_SensorPH,    buf); }
    if (ui_SensorTurb)  { snprintf(buf, sizeof(buf), "%.1f",   g_mqtt_turb);  lv_label_set_text(ui_SensorTurb,  buf); }
}

void ui_data_start(void)
{
    lv_timer_create(update_sensor_labels, 1000, NULL);
    mqtt_subscribe_start();
}
'''
with open(f, 'a') as fp:
    fp.write(code)
print('OK')
