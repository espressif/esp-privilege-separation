// Copyright 2020-2022 Espressif Systems (Shanghai) PTE LTD
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//         http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <string.h>
#include <freertos/FreeRTOS.h>
#include <esp_log.h>

#include <syscall_wrappers.h>
#include <syscall_def.h>
#include <rmaker_syscall.h>
#include <rmaker_syscall_priv.h>

#include <app_wifi.h>

#define RMAKER_DISPATCHER_TASK_PRIO 22

static const char *TAG = "rmaker_syscalls";

static QueueHandle_t usr_rmaker_dispatcher_queue;
static TaskHandle_t usr_rmaker_dispatcher_task_handle;

static void usr_rmaker_dispatcher_task(void *arg)
{
    usr_rmaker_dispatch_ctx_t dispatch_ctx = {};
    QueueHandle_t usr_queue = (QueueHandle_t)arg;
    while (1) {
        xQueueReceive(usr_queue, &dispatch_ctx, portMAX_DELAY);
        switch (dispatch_ctx.event) {
            case ESP_SYSCALL_EVENT_RMAKER: {
                usr_rmaker_write_cb_args_t *cb_args = (usr_rmaker_write_cb_args_t *)&dispatch_ctx.dispatch_data.write_cb_args;
                rmaker_dev_priv_data_t *data = (rmaker_dev_priv_data_t *)cb_args->priv_data;
                if (data) {
                    (*(data->app_write_cb))(cb_args->device, cb_args->param, cb_args->val, cb_args->priv_data, &cb_args->ctx);
                }
                break;
            }
            case ESP_SYSCALL_EVENT_BUTTON: {
                usr_button_cb_args_t *cb_args = (usr_button_cb_args_t *)&dispatch_ctx.dispatch_data.button_cb_args;
                (*cb_args->usr_cb)(cb_args->usr_args);
                break;
            }
            default:
                printf("Unknown event\n");
        }
    }
}

#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wint-conversion"
#endif

esp_rmaker_node_t *usr_esp_rmaker_node_init(const esp_rmaker_config_t *config, const char *name, const char *type)
{
    usr_rmaker_dispatcher_queue = xQueueCreate(20, sizeof(usr_rmaker_dispatch_ctx_t));
    if (!usr_rmaker_dispatcher_queue) {
        ESP_LOGE(TAG, "Failed to create dispatcher queue\nAborting...");
        abort();
    }
    xTaskCreate(usr_rmaker_dispatcher_task, "Rmaker user event dispatcher", CONFIG_PA_USER_DISPATCHER_TASK_STACK_SIZE, usr_rmaker_dispatcher_queue, RMAKER_DISPATCHER_TASK_PRIO, &usr_rmaker_dispatcher_task_handle);
    return EXECUTE_SYSCALL(config, name, strlen(name), type, strlen(type), usr_rmaker_dispatcher_queue, __NR_esp_rmaker_node_init);
}

esp_rmaker_device_t *usr_esp_rmaker_device_create(const char *name, const char *type, void *priv)
{
    return EXECUTE_SYSCALL(name, strlen(name), type, strlen(type), priv, __NR_esp_rmaker_device_create);
}

esp_err_t usr_esp_rmaker_device_add_cb(const esp_rmaker_device_t *device, esp_rmaker_device_write_cb_t write_cb, esp_rmaker_device_read_cb_t read_cb)
{
    return EXECUTE_SYSCALL(device, write_cb, read_cb, __NR_esp_rmaker_device_add_cb);
}

esp_err_t usr_esp_rmaker_device_add_param(const esp_rmaker_device_t *device, const esp_rmaker_param_t *param)
{
    return EXECUTE_SYSCALL(device, param, __NR_esp_rmaker_device_add_param);
}

esp_err_t usr_esp_rmaker_device_get_name(const esp_rmaker_device_t *device, char *buffer, int buffer_len)
{
    return EXECUTE_SYSCALL(device, buffer, buffer_len, __NR_esp_rmaker_device_get_name);
}

esp_rmaker_param_t *usr_esp_rmaker_param_create(const char *param_name, const char *type,
        esp_rmaker_param_val_t val, uint8_t properties)
{
    return EXECUTE_SYSCALL(param_name, strlen(param_name), type, strlen(type), &val, properties, __NR_esp_rmaker_param_create);
}

esp_err_t usr_esp_rmaker_device_assign_primary_param(const esp_rmaker_device_t *device, const esp_rmaker_param_t *param)
{
    return EXECUTE_SYSCALL(device, param, __NR_esp_rmaker_device_assign_primary_param);
}

esp_err_t usr_esp_rmaker_node_add_device(const esp_rmaker_node_t *node, const esp_rmaker_device_t *device)
{
    return EXECUTE_SYSCALL(node, device, __NR_esp_rmaker_node_add_device);
}

esp_err_t usr_esp_rmaker_start(void)
{
    return EXECUTE_SYSCALL(__NR_esp_rmaker_start);
}

esp_err_t usr_esp_rmaker_param_update_and_notify(const esp_rmaker_param_t *param, esp_rmaker_param_val_t val)
{
    return EXECUTE_SYSCALL(param, &val, __NR_esp_rmaker_param_update_and_notify);
}

esp_err_t usr_esp_rmaker_param_update_and_report(const esp_rmaker_param_t *param, esp_rmaker_param_val_t val)
{
    return EXECUTE_SYSCALL(param, &val, __NR_esp_rmaker_param_update_and_report);
}

esp_rmaker_param_t *usr_esp_rmaker_device_get_param_by_name(const esp_rmaker_device_t *device, const char *param_name)
{
    return EXECUTE_SYSCALL(device, param_name, strlen(param_name), __NR_esp_rmaker_device_get_param_by_name);
}

esp_rmaker_param_t *usr_esp_rmaker_device_get_param_by_type(const esp_rmaker_device_t *device, const char *param_type)
{
    return EXECUTE_SYSCALL(device, param_type, strlen(param_type), __NR_esp_rmaker_device_get_param_by_type);
}

esp_err_t usr_esp_rmaker_param_add_ui_type(const esp_rmaker_param_t *param, const char *ui_type)
{
    return EXECUTE_SYSCALL(param, ui_type, strlen(ui_type), __NR_esp_rmaker_param_add_ui_type);
}

esp_err_t usr_esp_rmaker_param_add_bounds(const esp_rmaker_param_t *param,
        esp_rmaker_param_val_t min, esp_rmaker_param_val_t max, esp_rmaker_param_val_t step)
{
    return EXECUTE_SYSCALL(param, &min, &max, &step, __NR_esp_rmaker_param_add_bounds);
}

esp_err_t usr_esp_rmaker_param_get_name(const esp_rmaker_param_t *param, char *buffer, int buffer_len)
{
    return EXECUTE_SYSCALL(param, buffer, buffer_len, __NR_esp_rmaker_param_get_name);
}

button_handle_t usr_iot_button_create(gpio_num_t gpio_num, button_active_t active_level)
{
    return EXECUTE_SYSCALL(gpio_num, active_level, __NR_iot_button_create);
}

esp_err_t usr_iot_button_set_evt_cb(button_handle_t btn_handle, button_cb_type_t type, button_cb cb, void* arg)
{
    return EXECUTE_SYSCALL(btn_handle, type, cb, arg, __NR_iot_button_set_evt_cb);
}

esp_err_t usr_app_reset_button_register(button_handle_t btn_handle, uint8_t wifi_reset_timeout,
        uint8_t factory_reset_timeout)
{
    return EXECUTE_SYSCALL(btn_handle, wifi_reset_timeout, factory_reset_timeout, __NR_app_reset_button_register);
}

esp_err_t usr_ws2812_led_init(void)
{
    return EXECUTE_SYSCALL(__NR_ws2812_led_init);
}

esp_err_t usr_ws2812_led_set_rgb(uint32_t red, uint32_t green, uint32_t blue)
{
    return EXECUTE_SYSCALL(red, green, blue, __NR_ws2812_led_set_rgb);
}

esp_err_t usr_ws2812_led_set_hsv(uint32_t hue, uint32_t saturation, uint32_t value)
{
    return EXECUTE_SYSCALL(hue, saturation, value, __NR_ws2812_led_set_hsv);
}

esp_err_t usr_ws2812_led_clear(void)
{
    return EXECUTE_SYSCALL(__NR_ws2812_led_clear);
}

esp_err_t usr_app_wifi_start(app_wifi_pop_type_t pop_type)
{
    return EXECUTE_SYSCALL(pop_type, __NR_app_wifi_start);
}

esp_err_t usr_esp_rmaker_kernel_init(void)
{
    return EXECUTE_SYSCALL(__NR_esp_rmaker_kernel_init);
}


#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif


