#include "logger.h"

static const char *TAG = "LOGGER";

void send_log_message(esp_log_level_t level, const char *tag, const char *format, ...) {
    log_message_t log_msg;
    va_list args;
    va_start(args, format);
    vsnprintf(log_msg.message, LOG_MESSAGE_LENGTH, format, args);
    va_end(args);
    log_msg.level = level;
    strncpy(log_msg.tag, tag, LOG_TAG_LENGTH - 1);
    log_msg.tag[LOG_TAG_LENGTH - 1] = '\0';  // Ensure null termination
    if (xQueueSend(log_queue, &log_msg, portMAX_DELAY) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to send log message to queue");
    }
}

void logger_task(void *pvParameter) {
    log_message_t log_msg;
    while (1) {
        if (xQueueReceive(log_queue, &log_msg, portMAX_DELAY)) {
            switch (log_msg.level) {
                case ESP_LOG_ERROR:
                    ESP_LOGE(TAG, "%s", log_msg.message);
                    break;
                case ESP_LOG_WARN:
                    ESP_LOGW(TAG, "%s", log_msg.message);
                    break;
                case ESP_LOG_INFO:
                    ESP_LOGI(TAG, "%s", log_msg.message);
                    break;
                case ESP_LOG_DEBUG:
                    ESP_LOGD(TAG, "%s", log_msg.message);
                    break;
                case ESP_LOG_VERBOSE:
                    ESP_LOGV(TAG, "%s", log_msg.message);
                    break;
                default:
                    ESP_LOGI(TAG, "%s", log_msg.message);
                    break;
            }
        }
    }
}
