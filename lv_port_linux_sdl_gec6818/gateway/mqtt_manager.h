#ifndef __MQTT_MANAGER_H
#define __MQTT_MANAGER_H
#include "gateway.h"
int  mqtt_publish(const char *broker, int port, const char *user, const char *pass, const char *topic, const char *payload);
int  mqtt_publish_local(SensorData *sd);
int  mqtt_upload_sensor(SensorData *sd);
void *mqtt_subscribe_thread(void *arg);
void *mqtt_heartbeat_thread(void *arg);
#endif
