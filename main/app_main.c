// System and peripheral includes
#include <string.h>
#include <inttypes.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <esp_log.h>
#include <esp_event.h>
#include <nvs_flash.h>

// RainMaker core includes
#include <esp_rmaker_core.h>
#include "esp_rmaker_utils.h"
#include <esp_rmaker_standard_types.h>
#include <esp_rmaker_standard_params.h>
#include <esp_rmaker_standard_devices.h>
#include <esp_rmaker_schedule.h>
#include <esp_rmaker_scenes.h>
#include <esp_rmaker_console.h>
#include <esp_rmaker_ota.h>
#include <esp_rmaker_common_events.h>

// App-specific includes
#include <app_network.h>
#include <app_insights.h>
#include "app_priv.h"
#include "driver/gpio.h"
#include "esp_insights.h"

// Pin and queue config
#define LED_GPIO 2
#define IR_QUEUE_LENGTH 5

static const char *TAG = "app_main";

// RainMaker devices and parameters
esp_rmaker_device_t *switch_device;
esp_rmaker_device_t *alert_device = NULL;
esp_rmaker_param_t *intruder_status_param = NULL;
QueueHandle_t ir_event_queue; // used to pass IR events between tasks

// IR event type
typedef enum {
    IR_TRIGGERED = 1
} ir_event_t;

// Callback for incoming RainMaker commands (switch toggles or reset trigger)
esp_err_t write_cb(const esp_rmaker_device_t *device, 
    const esp_rmaker_param_t *param,
    const esp_rmaker_param_val_t val,
    void *priv,
    esp_rmaker_write_ctx_t *ctx)
{
    const char *param_name = esp_rmaker_param_get_name(param);

    if (strcmp(param_name, "Power") == 0) {
        app_driver_set_state(val.val.b);
        esp_rmaker_param_update_and_report(param, val);
    } else if (strcmp(param_name, "Reset") == 0) {
        alert_reset_state = val.val.b;
        if (alert_reset_state) {
            ir_triggered = false;
            esp_rmaker_param_val_t new_val = esp_rmaker_bool(false);
            esp_rmaker_param_update_and_report(param, new_val);

            // Reset alert status back to 'Clear'
            esp_rmaker_param_update_and_report(intruder_status_param, esp_rmaker_str("Clear"));
            alert_reset_state = false;
        }
    }
    return ESP_OK;
}

// General event logger for RainMaker and provisioning events
static void event_handler(void* arg, esp_event_base_t event_base,
                          int32_t event_id, void* event_data)
{
    if (event_base == RMAKER_EVENT || event_base == RMAKER_COMMON_EVENT ||
        event_base == APP_NETWORK_EVENT || event_base == RMAKER_OTA_EVENT) {
        ESP_LOGI(TAG, "Event received: %s - ID: %" PRId32, event_base, event_id);
    } else {
        ESP_LOGW(TAG, "Invalid event received!");
    }
}

// Task that monitors the IR sensor flag and pushes events into the queue
static void ir_monitor_task(void *arg) {
    bool prev_state = false;
    while (1) {
        if (ir_triggered && !prev_state) {
            ir_event_t event = IR_TRIGGERED;
            xQueueSend(ir_event_queue, &event, portMAX_DELAY);
            prev_state = true;
        } else if (!ir_triggered) {
            prev_state = false;
        }
        vTaskDelay(pdMS_TO_TICKS(100)); // small debounce delay
    }
}

// Task that waits for an IR trigger and then updates the RainMaker dashboard
static void alert_task(void *arg) {
    ir_event_t event;
    while (1) {
        if (xQueueReceive(ir_event_queue, &event, portMAX_DELAY)) {
            if (!alert_reset_state) {
                ESP_LOGI(TAG, "Intruder Alert Triggered 🚨");
                esp_rmaker_param_update_and_report(intruder_status_param, esp_rmaker_str("Alerted"));
            }
        }
    }
}

void app_main()
{
    // Init console and app-specific drivers
    esp_rmaker_console_init();
    app_driver_init();
    app_driver_set_state(DEFAULT_POWER);

    // Init NVS flash (used by RainMaker and WiFi provisioning)
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    app_network_init();

    // Register all RainMaker-related event handlers
    ESP_ERROR_CHECK(esp_event_handler_register(RMAKER_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(RMAKER_COMMON_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(APP_NETWORK_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(RMAKER_OTA_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));

    // Init RainMaker node
    esp_rmaker_config_t rainmaker_cfg = { .enable_time_sync = true };
    esp_rmaker_node_t *node = esp_rmaker_node_init(&rainmaker_cfg, "ESP32", "IoT_Project");
    if (!node) {
        ESP_LOGE(TAG, "Could not initialise node. Aborting!!!");
        vTaskDelay(5000 / portTICK_PERIOD_MS);
        abort();
    }

    // Create Lights switch device (GPIO 2 LED control)
    switch_device = esp_rmaker_device_create("Lights", ESP_RMAKER_DEVICE_SWITCH, NULL);
    esp_rmaker_device_add_cb(switch_device, write_cb, NULL);
    esp_rmaker_device_add_param(switch_device, esp_rmaker_name_param_create(ESP_RMAKER_DEF_NAME_PARAM, "Lights"));

    esp_rmaker_param_t *power_param = esp_rmaker_param_create("Power", ESP_RMAKER_PARAM_POWER,
        esp_rmaker_bool(false), PROP_FLAG_READ | PROP_FLAG_WRITE);
    gpio_set_level(LED_GPIO, esp_rmaker_param_get_val(power_param)->val.b);
    esp_rmaker_device_add_param(switch_device, power_param);
    esp_rmaker_device_assign_primary_param(switch_device, power_param);
    esp_rmaker_node_add_device(node, switch_device);

    esp_rmaker_param_val_t *val_ptr = esp_rmaker_param_get_val(power_param);
    if (val_ptr) {
        write_cb(switch_device, power_param, *val_ptr, NULL, NULL);
    }

    // Create Alert Reset switch device
    alert_device = esp_rmaker_device_create("Intruder Alert Reset", ESP_RMAKER_DEVICE_SWITCH, NULL);
    esp_rmaker_device_add_cb(alert_device, write_cb, NULL);

    esp_rmaker_param_t *alert_reset_param = esp_rmaker_param_create(
        "Reset", ESP_RMAKER_PARAM_POWER, esp_rmaker_bool(false),
        PROP_FLAG_READ | PROP_FLAG_WRITE);
    esp_rmaker_device_add_param(alert_device, alert_reset_param);
    esp_rmaker_device_assign_primary_param(alert_device, alert_reset_param);

    // Add Intruder_Status string param (custom alert indicator)
    intruder_status_param = esp_rmaker_param_create(
        "Intruder_Status",
        "esp.param.intruder_status",
        esp_rmaker_str("Clear"),
        PROP_FLAG_READ | PROP_FLAG_WRITE);
    esp_rmaker_device_add_param(alert_device, intruder_status_param);

    esp_rmaker_node_add_device(node, alert_device);

    // Enable optional RainMaker services
    esp_rmaker_ota_enable_default();
    esp_rmaker_timezone_service_enable();
    esp_rmaker_schedule_enable();
    esp_rmaker_scenes_enable();
    app_insights_enable();

    esp_rmaker_start();

    // Start WiFi provisioning or connect if already provisioned
    err = app_network_set_custom_mfg_data(MGF_DATA_DEVICE_TYPE_SWITCH, MFG_DATA_DEVICE_SUBTYPE_SWITCH);
    err = app_network_start(POP_TYPE_RANDOM);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Could not start Wifi. Aborting!!!");
        vTaskDelay(5000 / portTICK_PERIOD_MS);
        abort(); 
    }

    // Create FreeRTOS queue and launch tasks
    ir_event_queue = xQueueCreate(IR_QUEUE_LENGTH, sizeof(ir_event_t));
    xTaskCreate(ir_monitor_task, "ir_monitor_task", 2048, NULL, 4, NULL);
    xTaskCreate(alert_task, "alert_task", 4096, NULL, 3, NULL);
}
