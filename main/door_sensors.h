#ifndef DOOR_SENSORS_H
#define DOOR_SENSORS_H

// GPIO pin definitions for the sensors
#define LEFT_SENSOR_GPIO GPIO_NUM_4
#define RIGHT_SENSOR_GPIO GPIO_NUM_15

#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"
#include "mqtt_client.h"

// Enum to define door status
typedef enum { DOOR_STATUS_UNKNOWN = 0, DOOR_STATUS_OPEN, DOOR_STATUS_CLOSED, DOOR_STATUS_ERROR } door_status_t;

// Function to initialize the GPIO pins for the sensors
void init_sensors_gpio(void);

// Function to publish the door status to the specified MQTT topic
static void publish_door_status(esp_mqtt_client_handle_t client, door_status_t status, const char *mqtt_topic);

// Function to read the door status from the sensors
static door_status_t read_door_status(void);

// Timer callback function to read the door status and publish if it has changed
static void status_timer_callback(TimerHandle_t xTimer);

// Function to initialize the status timer
void init_status_timer(esp_mqtt_client_handle_t client, const char *mqtt_topic, int transmit_interval_minutes);

// Function to initialize the door sensors
void init_door_sensors(esp_mqtt_client_handle_t client, const char *mqtt_topic, int transmit_interval_minutes);

#endif  // DOOR_SENSORS_H
