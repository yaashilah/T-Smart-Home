#ifndef APP_PRIV_H
#define APP_PRIV_H

#include <stdbool.h>
#include <stdint.h>
#include <esp_rmaker_core.h>

#define DEFAULT_POWER true

// Devices
extern esp_rmaker_device_t *switch_device;
extern esp_rmaker_device_t *alert_device;

// Shared state variables
extern bool ir_triggered;
extern bool alert_reset_state;

// Driver API
void app_driver_init(void);
int app_driver_set_state(bool state);
bool app_driver_get_state(void);

#endif // APP_PRIV_H
