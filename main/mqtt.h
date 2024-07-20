#ifndef MQTT_H
#define MQTT_H
#include <stdbool.h>

#include "mqtt_client.h"  // Add this line to ensure esp_mqtt_client_handle_t is recognized

extern esp_mqtt_client_handle_t client;
extern bool is_mqtt_connected;

esp_mqtt_client_handle_t mqtt_app_start(void);
void mqtt_subscribe_task(void *pvParameters);
bool mqtt_client_is_connected(void);

#endif  // MQTT_H
