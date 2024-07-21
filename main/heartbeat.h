#ifndef HEARTBEAT_H
#define HEARTBEAT_H

#include "cJSON.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mqtt_client.h"

// Function to publish heartbeat message
void publish_heartbeat(esp_mqtt_client_handle_t client, const char *thing_name);

// Task to send heartbeat messages
void heartbeat_task(void *pvParameters);

#endif  // HEARTBEAT_H
