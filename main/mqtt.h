#ifndef MQTT_H
#define MQTT_H
#include "mqtt_client.h" // Add this line to ensure esp_mqtt_client_handle_t is recognized
#include <stdbool.h>

extern esp_mqtt_client_handle_t client;
extern bool is_mqtt_connected;

void mqtt_app_start(void);
void mqtt_subscribe_task(void *pvParameters);
bool mqtt_client_is_connected(void);

#define COOP_HARDWARE_SIGNAL "coop/hardware/signal"
#define COOP_STATUS_TOPIC "coop/status"
#define COOP_OTA_UPDATE_CONTROLLER_TOPIC "coop/update/controller"

#endif // MQTT_H
