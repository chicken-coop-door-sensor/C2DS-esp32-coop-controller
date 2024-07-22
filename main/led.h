#ifndef LED_H
#define LED_H

#include <stdint.h>

#include "driver/ledc.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include "string.h"

#define RED_PIN GPIO_NUM_25
#define GREEN_PIN GPIO_NUM_26
#define BLUE_PIN GPIO_NUM_27

typedef enum {
    LED_OFF,
    LED_RED,
    LED_GREEN,
    LED_BLUE,
    LED_CYAN,
    LED_MAGENTA,
    LED_FLASHING_RED,
    LED_FLASHING_GREEN,
    LED_FLASHING_BLUE,
    LED_FLASHING_WHITE,
    LED_FLASHING_YELLOW,
    LED_FLASHING_CYAN,
    LED_FLASHING_MAGENTA,
    LED_FLASHING_ORANGE,
    LED_PULSATING_RED,
    LED_PULSATING_GREEN,
    LED_PULSATING_BLUE,
    LED_PULSATING_WHITE
} led_state_t;

extern QueueHandle_t led_queue;

void init_led_pwm(void);
void set_led_color(uint32_t red, uint32_t green, uint32_t blue);
void led_task(void *pvParameter);
void set_led(led_state_t new_state);
led_state_t lookup_led_state(const char *str);

#endif  // LED_H
