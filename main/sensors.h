#ifndef SENSORS_H
#define SENSORS_H

#include "driver/gpio.h"
#include "mqtt_client.h"

// GPIO pin definitions for the sensors
#define LEFT_SENSOR_GPIO GPIO_NUM_4
#define RIGHT_SENSOR_GPIO GPIO_NUM_15

// Enum to define door status
typedef enum { DOOR_STATUS_UNKNOWN = 0, DOOR_STATUS_OPEN, DOOR_STATUS_CLOSED, DOOR_STATUS_ERROR } door_status_t;

// Function declarations
void init_sensors_gpio(void);
void init_status_timer(esp_mqtt_client_handle_t client);
void read_sensors_task(void *pvParameters);
void app_main(void);

// Utility function to read and validate sensor values
static door_status_t read_door_status(void);

// Callback for the status timer
static void status_timer_callback(TimerHandle_t xTimer);

// Function to publish door status via MQTT
static void publish_door_status(esp_mqtt_client_handle_t client, door_status_t status);

#endif  // SENSORS_H
