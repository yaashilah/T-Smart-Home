#include <stdio.h>
#include <stdbool.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <driver/gpio.h>
#include <driver/ledc.h>
#include <esp_log.h>
#include <esp_rmaker_core.h>
#include "app_priv.h"

// GPIO Pins
#define LED_GPIO           2    // LED for Light system
#define IR_SENSOR_GPIO     3    // IR Sensor for Intruder alert system
#define ALERT_LED_GPIO     4    // Red LED for Intruder alert System
#define BUZZER_GPIO        5

// Buzzer Config
#define BUZZER_DUTY        900
#define BUZZER_FREQ_HZ     10000

// States
bool ir_triggered = false;
bool alert_reset_state = false;
static bool notification_sent = false;

// Buzzer Functions
void buzzer_on() {
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, BUZZER_DUTY);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}

void buzzer_off() {
    ledc_stop(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 0);
}

void buzzer_init() {
    ledc_timer_config_t timer_conf = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .timer_num = LEDC_TIMER_0,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .freq_hz = BUZZER_FREQ_HZ,
        .clk_cfg = LEDC_AUTO_CLK
    };
    ledc_timer_config(&timer_conf);

    ledc_channel_config_t channel_conf = {
        .gpio_num = BUZZER_GPIO,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = LEDC_TIMER_0,
        .duty = 0,
        .hpoint = 0
    };
    ledc_channel_config(&channel_conf);
}

// IR Sensor Monitoring Task
void ir_sensor_task(void *arg) {
    while (1) {
        int val = gpio_get_level(IR_SENSOR_GPIO);
        bool active = (val == 0);  // Active Low

        if (active && !ir_triggered && !alert_reset_state) {
            ir_triggered = true;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

// Alert Output Task
void alert_output_task(void *arg) {
    while (1) {
        if (ir_triggered && !alert_reset_state) {
            gpio_set_level(ALERT_LED_GPIO, 1);

            if (!notification_sent) {
                esp_rmaker_raise_alert("Intruder Alert 🚨");
                notification_sent = true;
            }

            // Proper buzzer beeping using PWM
            for (int i = 0; i < 5; i++) {
                buzzer_on();
                vTaskDelay(pdMS_TO_TICKS(200));
                buzzer_off();
                vTaskDelay(pdMS_TO_TICKS(200));
            }

        } else {
            gpio_set_level(ALERT_LED_GPIO, 0);
            buzzer_off();
            notification_sent = false;
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}


// App Driver Init
void app_driver_init() {
    // LED GPIO
    gpio_config_t led_conf = {
        .pin_bit_mask = (1ULL << LED_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = 1,
    };
    gpio_config(&led_conf);

    // IR Sensor GPIO
    gpio_config_t ir_conf = {
        .pin_bit_mask = (1ULL << IR_SENSOR_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = 0,
        .pull_down_en = 0,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&ir_conf);

    // Alert LED GPIO
    gpio_config_t alert_led_conf = {
        .pin_bit_mask = (1ULL << ALERT_LED_GPIO),
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&alert_led_conf);

    // Buzzer GPIO
    gpio_config_t buzzer_conf = {
        .pin_bit_mask = (1ULL << BUZZER_GPIO),
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&buzzer_conf);

    buzzer_init();

    xTaskCreate(ir_sensor_task, "ir_sensor_task", 2048, NULL, 5, NULL);
    xTaskCreate(alert_output_task, "alert_output_task", 2048, NULL, 5, NULL);
}

// Optional LED state control if needed
static bool g_power_state = DEFAULT_POWER;

int IRAM_ATTR app_driver_set_state(bool state) {
    if (g_power_state != state) {
        g_power_state = state;
        gpio_set_level(LED_GPIO, g_power_state);

        if (!g_power_state) {
            alert_reset_state = false;
            notification_sent = false;
        }
    }
    return ESP_OK;
}

bool app_driver_get_state(void) {
    return g_power_state;
}
