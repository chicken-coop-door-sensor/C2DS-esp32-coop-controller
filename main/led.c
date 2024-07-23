#include "led.h"

static const char *TAG = "LED";
led_state_t current_led_state = LED_OFF;

void init_led_pwm(void) {
    ESP_LOGI(TAG, "Initializing LED PWM...");

    ledc_timer_config_t ledc_timer = {.speed_mode = LEDC_LOW_SPEED_MODE,
                                      .timer_num = LEDC_TIMER_0,
                                      .duty_resolution = LEDC_TIMER_13_BIT,
                                      .freq_hz = 5000,
                                      .clk_cfg = LEDC_AUTO_CLK};
    ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));

    ledc_channel_config_t ledc_channel = {.channel = LEDC_CHANNEL_0,
                                          .duty = 0,
                                          .gpio_num = RED_PIN,
                                          .speed_mode = LEDC_LOW_SPEED_MODE,
                                          .hpoint = 0,
                                          .timer_sel = LEDC_TIMER_0};
    ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel));

    ledc_channel.channel = LEDC_CHANNEL_1;
    ledc_channel.gpio_num = GREEN_PIN;
    ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel));

    ledc_channel.channel = LEDC_CHANNEL_2;
    ledc_channel.gpio_num = BLUE_PIN;
    ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel));
}

void set_led_color(uint32_t red, uint32_t green, uint32_t blue) {
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, red);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);

    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1, green);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1);

    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_2, blue);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_2);
}

void flash_led_color(uint32_t red, uint32_t green, uint32_t blue) {
    set_led_color(red, green, blue);  // Turn on the LED
    vTaskDelay(pdMS_TO_TICKS(500));   // Wait for 500 ms
    set_led_color(0, 0, 0);           // Turn off the LED
    vTaskDelay(pdMS_TO_TICKS(500));   // Wait for 500 ms
}

void pulsate_led_color(uint32_t red, uint32_t green, uint32_t blue) {
    const int max_duty = 8191;  // Maximum duty cycle for 13-bit resolution
    const int step = 50;        // Step size for duty cycle change
    const int delay = 40;       // Delay in milliseconds for each step

    // Increase brightness
    for (int duty = 0; duty <= max_duty; duty += step) {
        set_led_color((red * duty) / max_duty, (green * duty) / max_duty, (blue * duty) / max_duty);
        vTaskDelay(pdMS_TO_TICKS(delay));
    }

    // Decrease brightness
    for (int duty = max_duty; duty >= 0; duty -= step) {
        set_led_color((red * duty) / max_duty, (green * duty) / max_duty, (blue * duty) / max_duty);
        vTaskDelay(pdMS_TO_TICKS(delay));
    }
}

// Function to map string to enum
led_state_t convert_led_string_to_enum(const char *str) {
    ESP_LOGI(TAG, "Looking up LED value for: %s", str);

    led_state_t led_state = LED_OFF;

    if (strcmp(str, "LED_OFF") == 0) {
        led_state = LED_OFF;
    } else if (strcmp(str, "LED_RED") == 0) {
        led_state = LED_RED;
    } else if (strcmp(str, "LED_GREEN") == 0) {
        led_state = LED_GREEN;
    } else if (strcmp(str, "LED_BLUE") == 0) {
        led_state = LED_BLUE;
    } else if (strcmp(str, "LED_CYAN") == 0) {
        led_state = LED_CYAN;
    } else if (strcmp(str, "LED_MAGENTA") == 0) {
        led_state = LED_MAGENTA;
    } else if (strcmp(str, "LED_FLASHING_RED") == 0) {
        led_state = LED_FLASHING_RED;
    } else if (strcmp(str, "LED_FLASHING_GREEN") == 0) {
        led_state = LED_FLASHING_GREEN;
    } else if (strcmp(str, "LED_FLASHING_BLUE") == 0) {
        led_state = LED_FLASHING_BLUE;
    } else if (strcmp(str, "LED_FLASHING_WHITE") == 0) {
        led_state = LED_FLASHING_WHITE;
    } else if (strcmp(str, "LED_FLASHING_YELLOW") == 0) {
        led_state = LED_FLASHING_YELLOW;
    } else if (strcmp(str, "LED_FLASHING_CYAN") == 0) {
        led_state = LED_FLASHING_CYAN;
    } else if (strcmp(str, "LED_FLASHING_MAGENTA") == 0) {
        led_state = LED_FLASHING_MAGENTA;
    } else if (strcmp(str, "LED_FLASHING_ORANGE") == 0) {
        led_state = LED_FLASHING_ORANGE;
    } else if (strcmp(str, "LED_PULSATING_RED") == 0) {
        led_state = LED_PULSATING_RED;
    } else if (strcmp(str, "LED_PULSATING_GREEN") == 0) {
        led_state = LED_PULSATING_GREEN;
    } else if (strcmp(str, "LED_PULSATING_BLUE") == 0) {
        led_state = LED_PULSATING_BLUE;
    } else if (strcmp(str, "LED_PULSATING_WHITE") == 0) {
        led_state = LED_PULSATING_WHITE;
    } else {
        led_state = LED_OFF;
    }

    ESP_LOGI(TAG, "Returning: %d", led_state);

    return led_state;
}

const char *convert_led_enum_to_string(led_state_t state) {
    const char *result;
    if (state == LED_OFF) {
        result = "LED_OFF";
    } else if (state == LED_RED) {
        result = "LED_RED";
    } else if (state == LED_GREEN) {
        result = "LED_GREEN";
    } else if (state == LED_BLUE) {
        result = "LED_BLUE";
    } else if (state == LED_CYAN) {
        result = "LED_CYAN";
    } else if (state == LED_MAGENTA) {
        result = "LED_MAGENTA";
    } else if (state == LED_FLASHING_RED) {
        result = "LED_FLASHING_RED";
    } else if (state == LED_FLASHING_GREEN) {
        result = "LED_FLASHING_GREEN";
    } else if (state == LED_FLASHING_BLUE) {
        result = "LED_FLASHING_BLUE";
    } else if (state == LED_FLASHING_WHITE) {
        result = "LED_FLASHING_WHITE";
    } else if (state == LED_FLASHING_YELLOW) {
        result = "LED_FLASHING_YELLOW";
    } else if (state == LED_FLASHING_CYAN) {
        result = "LED_FLASHING_CYAN";
    } else if (state == LED_FLASHING_MAGENTA) {
        result = "LED_FLASHING_MAGENTA";
    } else if (state == LED_FLASHING_ORANGE) {
        result = "LED_FLASHING_ORANGE";
    } else if (state == LED_PULSATING_RED) {
        result = "LED_PULSATING_RED";
    } else if (state == LED_PULSATING_GREEN) {
        result = "LED_PULSATING_GREEN";
    } else if (state == LED_PULSATING_BLUE) {
        result = "LED_PULSATING_BLUE";
    } else if (state == LED_PULSATING_WHITE) {
        result = "LED_PULSATING_WHITE";
    } else {
        result = "UNKNOWN_STATE";
    }
    return result;
}

void set_led(led_state_t new_state) {
    ESP_LOGI(TAG, "Queuing request to set LED to: %s", convert_led_enum_to_string(new_state));
    if (xQueueSend(led_queue, &new_state, portMAX_DELAY) != pdPASS) {
        ESP_LOGW(TAG, "Failed to send message to LED queue");
    } else {
        ESP_LOGI(TAG, "Successfully sent message to LED queue");
    }
}

void led_task(void *pvParameter) {
    led_state_t new_led_state = LED_OFF;

    while (1) {
        if (xQueueReceive(led_queue, &new_led_state, portMAX_DELAY)) {
            if (new_led_state != current_led_state) {
                ESP_LOGI(TAG, "Rx request to set LED state %s to %s", convert_led_enum_to_string(current_led_state),
                         convert_led_enum_to_string(new_led_state));
                current_led_state = new_led_state;
            } else {
                continue;
            }
        } else {
            ESP_LOGE(TAG, "Failed to receive message from LED queue");
        }
        switch (current_led_state) {
            case LED_OFF:
                ESP_LOGE(TAG, "Setting LED to OFF");
                set_led_color(0, 0, 0);
                break;
            case LED_RED:
                set_led_color(8191, 0, 0);
                break;
            case LED_GREEN:
                ESP_LOGI(TAG, "Setting LED to GREEN");
                set_led_color(0, 8191, 0);
                break;
            case LED_BLUE:
                set_led_color(0, 0, 8191);
                break;
            case LED_CYAN:
                set_led_color(0, 8191, 8191);
                break;
            case LED_MAGENTA:
                set_led_color(8191, 0, 8191);
                break;
            case LED_FLASHING_RED:
                ESP_LOGI(TAG, "Setting LED to FLASHING RED");
                flash_led_color(8191, 0, 0);  // Flash RED
                break;
            case LED_FLASHING_GREEN:
                ESP_LOGI(TAG, "Setting LED to FLASHING GREEN");
                flash_led_color(0, 8191, 0);  // Flash GREEN
                break;
            case LED_FLASHING_BLUE:
                flash_led_color(0, 0, 8191);  // Flash BLUE
                break;
            case LED_FLASHING_WHITE:
                flash_led_color(8191, 8191, 8191);  // Flash WHITE
                break;
            case LED_FLASHING_YELLOW:
                flash_led_color(8191, 8191, 0);  // Flash YELLOW
                break;
            case LED_FLASHING_CYAN:
                flash_led_color(0, 8191, 8191);  // Flash CYAN
                break;
            case LED_FLASHING_MAGENTA:
                flash_led_color(8191, 0, 8191);  // Flash MAGENTA
                break;
            case LED_FLASHING_ORANGE:
                flash_led_color(8191, 4096, 0);  // Flash ORANGE
                break;
            case LED_PULSATING_RED:
                pulsate_led_color(8191, 0, 0);
                break;
            case LED_PULSATING_GREEN:
                pulsate_led_color(0, 8191, 0);
                break;
            case LED_PULSATING_BLUE:
                pulsate_led_color(0, 0, 8191);
                break;
            case LED_PULSATING_WHITE:
                pulsate_led_color(8191, 8191, 8191);
                break;
            default:
                set_led_color(0, 0, 0);  // Ensure LED is off
                ESP_LOGE(TAG, "Invalid LED state! Setting LED to OFF");
                break;
        }
        vTaskDelay(pdMS_TO_TICKS(1000));  // Delay for 100 milliseconds
    }
}
