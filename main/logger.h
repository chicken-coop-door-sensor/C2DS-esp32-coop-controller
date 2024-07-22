#ifndef LOGGER_H
#define LOGGER_H

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "stdio.h"
#include "string.h"

#define LOG_QUEUE_LENGTH 10
#define LOG_MESSAGE_LENGTH 256
#define LOG_TAG_LENGTH 32

typedef struct {
    char tag[LOG_TAG_LENGTH];
    char message[LOG_MESSAGE_LENGTH];
    esp_log_level_t level;
} log_message_t;

extern QueueHandle_t log_queue;

void logger_task(void *pvParameter);
void send_log_message(esp_log_level_t level, const char *tag, const char *format, ...);
#endif  // LOGGER_H
