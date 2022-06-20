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

#include <stdio.h>
#include <string.h>
#include <freertos/FreeRTOS.h>
#include <soc_defs.h>

#include <esp_log.h>
#include <nvs_flash.h>
#include <esp_rmaker_console.h>
#include <esp_rmaker_standard_params.h>
#include <esp_rmaker_common_events.h>
#include <esp_rmaker_ota.h>

#include <ws2812_led.h>
#include <app_wifi.h>
#include <rmaker_syscall_priv.h>

#include <app_reset.h>
#include <esp_map.h>

#define ESP_MAP_RMAKER_NODE_ID      0xF5A8
#define ESP_MAP_RMAKER_DEVICE_ID    0xF5A9
#define ESP_MAP_RMAKER_PARAM_ID     0xF5AA
#define ESP_MAP_RMAKER_BUTTON_ID    0xF5AB

static const char *TAG = "rmaker_syscalls";

static QueueHandle_t sys_rmaker_dispatcher_queue;

esp_err_t esp_rmaker_param_delete(const esp_rmaker_param_t *param);
esp_err_t sys_write_cb(const esp_rmaker_device_t *device, const esp_rmaker_param_t *param,
        const esp_rmaker_param_val_t val, void *priv_data, esp_rmaker_write_ctx_t *ctx);

esp_rmaker_node_t *sys_esp_rmaker_node_init(const esp_rmaker_config_t *config, const char *name, int name_len, const char *type, int type_len, QueueHandle_t usr_queue)
{
    if (!is_valid_user_d_addr((void *)config) || !is_valid_user_d_addr((void *)name) ||
        !is_valid_user_d_addr((void *)(name + name_len)) || !is_valid_user_d_addr((void *)type) ||
        !is_valid_user_d_addr((void *)(type + type_len)) || *(name + name_len) != '\0' ||
        *(type + type_len) != '\0') {
        return NULL;
    }
    sys_rmaker_dispatcher_queue = (esp_map_get_handle((int)usr_queue))->handle;
    esp_rmaker_node_t *node = esp_rmaker_node_init(config, name, type);
    if (!node) {
        return NULL;
    }
    int wrapper_index = esp_map_add(node, ESP_MAP_RMAKER_NODE_ID);
    if (!wrapper_index) {
        esp_rmaker_node_deinit(node);
        return NULL;
    }
    return (esp_rmaker_node_t *)wrapper_index;
}

esp_rmaker_device_t *sys_esp_rmaker_device_create(const char *name, int name_len, const char *type, int type_len, void *priv)
{
    if (!is_valid_user_d_addr((void *)name) || !is_valid_user_d_addr((void *)(name + name_len)) ||
        !is_valid_user_d_addr((void *)type) || !is_valid_user_d_addr((void *)(type + type_len)) ||
        !is_valid_user_d_addr((void *)priv) || *(name + name_len) != '\0' ||
        *(type + type_len) != '\0') {
        return NULL;
    }
    esp_rmaker_device_t *device = esp_rmaker_device_create(name, type, priv);
    if (!device) {
        return NULL;
    }
    if (priv) {
        esp_err_t err = esp_rmaker_device_add_cb(device, sys_write_cb, NULL);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to add device write_cb, 0x%x", err);
            esp_rmaker_device_delete(device);
            return NULL;
        }
    }

    int wrapper_index = esp_map_add(device, ESP_MAP_RMAKER_DEVICE_ID);
    if (!wrapper_index) {
        esp_rmaker_device_delete(device);
        return NULL;
    }
    return (esp_rmaker_device_t *)wrapper_index;
}

esp_err_t sys_write_cb(const esp_rmaker_device_t *device, const esp_rmaker_param_t *param,
        const esp_rmaker_param_val_t val, void *priv_data, esp_rmaker_write_ctx_t *ctx)
{
    if (!sys_rmaker_dispatcher_queue) {
        return ESP_FAIL;
    }

    int user_handles = esp_map_get_allocated_size();
    int device_index = 0, param_index = 0;
    for (int i = ESP_MAP_INDEX_OFFSET; i < (ESP_MAP_INDEX_OFFSET + user_handles); i++) {
        esp_map_handle_t *wrapper_handle = esp_map_get_handle(i);
        if (!wrapper_handle) {
            continue;
        } else if (device == wrapper_handle->handle) {
            device_index = i;
        } else if (param == wrapper_handle->handle) {
            param_index = i;
        }
        if (device_index && param_index) {
            break;
        }
    }
    if (!device_index || !param_index) {
        ESP_LOGE(TAG, "Failed to get indexes in esp_map");
        return ESP_FAIL;
    }
    usr_rmaker_write_cb_args_t cb_args = {
        .device = (const esp_rmaker_device_t *)device_index,
        .param = (const esp_rmaker_param_t *)param_index,
        .val = val,
        .priv_data = priv_data,
        .ctx = *ctx,
    };

    usr_rmaker_dispatch_ctx_t dispatch_ctx = {
        .event = ESP_SYSCALL_EVENT_RMAKER,
    };
    memcpy(&dispatch_ctx.dispatch_data.write_cb_args, &cb_args, sizeof(usr_rmaker_write_cb_args_t));
    if (xQueueSend(sys_rmaker_dispatcher_queue, &dispatch_ctx, portMAX_DELAY) == pdFALSE) {
        ESP_LOGE(TAG, "Error sending message on queue");
    }
    return ESP_OK;
}

esp_err_t sys_esp_rmaker_device_add_cb(const esp_rmaker_device_t *device, esp_rmaker_device_write_cb_t write_cb, esp_rmaker_device_read_cb_t read_cb)
{
    ESP_LOGW(TAG, "esp_rmaker_device_add_cb does not add the callback! This system call is provided for example compatibility.\nPlease refer to rmaker_dev_priv_data_t struct usage for adding a callback.");
    return ESP_OK;
}

esp_err_t sys_esp_rmaker_device_add_param(const esp_rmaker_device_t *device, const esp_rmaker_param_t *param)
{
    int device_wrapper_index = (int)device;
    esp_map_handle_t *device_wrapper_handle = esp_map_verify(device_wrapper_index, ESP_MAP_RMAKER_DEVICE_ID);
    if (!device_wrapper_handle) {
        return ESP_ERR_INVALID_ARG;
    }
    int param_wrapper_index = (int)param;
    esp_map_handle_t *param_wrapper_handle = esp_map_verify(param_wrapper_index, ESP_MAP_RMAKER_PARAM_ID);
    if (!param_wrapper_handle) {
        return ESP_ERR_INVALID_ARG;
    }
    return esp_rmaker_device_add_param((const esp_rmaker_device_t *)device_wrapper_handle->handle, (const esp_rmaker_param_t *)param_wrapper_handle->handle);
}

esp_err_t sys_esp_rmaker_device_get_name(const esp_rmaker_param_t *device, char *buffer, int buffer_len)
{
    if (!is_valid_udram_addr((void *)buffer) || !is_valid_udram_addr((void *)(buffer + buffer_len))) {
        return ESP_ERR_INVALID_ARG;
    }
    int wrapper_index = (int)device;
    if (!is_valid_udram_addr(buffer) || !is_valid_udram_addr(buffer + buffer_len)) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_map_handle_t *wrapper_handle = esp_map_verify(wrapper_index, ESP_MAP_RMAKER_DEVICE_ID);
    if (!wrapper_handle) {
        return ESP_ERR_INVALID_ARG;
    }
    char *device_name = esp_rmaker_device_get_name((const esp_rmaker_device_t *)wrapper_handle->handle);
    strlcpy(buffer, device_name, buffer_len);
    return ESP_OK;
}

esp_rmaker_param_t *sys_esp_rmaker_param_create(const char *param_name, int param_len, const char *type, int type_len, esp_rmaker_param_val_t *val, uint8_t properties)
{
    if (!is_valid_user_d_addr((void *)param_name) || !is_valid_user_d_addr((void *)(param_name + param_len)) ||
        !is_valid_user_d_addr((void *)type) || !is_valid_user_d_addr((void *)(type + type_len)) ||
        !is_valid_user_d_addr((void *)val) || *(param_name + param_len) != '\0' ||
        *(type + type_len) != '\0') {
        return NULL;
    }
    esp_rmaker_param_t *param = esp_rmaker_param_create(param_name, type, *val, properties);
    if (!param) {
        return NULL;
    }
    int wrapper_index = esp_map_add(param, ESP_MAP_RMAKER_PARAM_ID);
    if (!wrapper_index) {
        esp_rmaker_param_delete(param);
        return NULL;
    }
    return (esp_rmaker_param_t *)wrapper_index;
}

esp_err_t sys_esp_rmaker_device_assign_primary_param(const esp_rmaker_device_t *device, const esp_rmaker_param_t *param)
{
    int device_wrapper_index = (int)device;
    esp_map_handle_t *device_wrapper_handle = esp_map_verify(device_wrapper_index, ESP_MAP_RMAKER_DEVICE_ID);
    if (!device_wrapper_handle) {
        return ESP_ERR_INVALID_ARG;
    }
    int param_wrapper_index = (int)param;
    esp_map_handle_t *param_wrapper_handle = esp_map_verify(param_wrapper_index, ESP_MAP_RMAKER_PARAM_ID);
    if (!device_wrapper_handle) {
        return ESP_ERR_INVALID_ARG;
    }
    return esp_rmaker_device_assign_primary_param((const esp_rmaker_device_t *)device_wrapper_handle->handle, (const esp_rmaker_param_t *)param_wrapper_handle->handle);
}

esp_err_t sys_esp_rmaker_node_add_device(const esp_rmaker_node_t *node, const esp_rmaker_device_t *device)
{
    int node_wrapper_index = (int)node;
    esp_map_handle_t *node_wrapper_handle = esp_map_verify(node_wrapper_index, ESP_MAP_RMAKER_NODE_ID);
    if (!node_wrapper_handle) {
        return ESP_ERR_INVALID_ARG;
    }
    int device_wrapper_index = (int)device;
    esp_map_handle_t *device_wrapper_handle = esp_map_verify(device_wrapper_index, ESP_MAP_RMAKER_DEVICE_ID);
    if (!device_wrapper_handle) {
        return ESP_ERR_INVALID_ARG;
    }
    return esp_rmaker_node_add_device((const esp_rmaker_node_t *)node_wrapper_handle->handle, (const esp_rmaker_device_t *)device_wrapper_handle->handle);
}

esp_err_t sys_esp_rmaker_start(void)
{
    /* Enable OTA */
    esp_rmaker_ota_config_t ota_config = {
        .server_cert = ESP_RMAKER_OTA_DEFAULT_SERVER_CERT,
    };
    esp_err_t err = esp_rmaker_ota_enable(&ota_config, OTA_USING_PARAMS);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enable Rmaker OTA, 0x%x", err);
        return err;
    }
    return esp_rmaker_start();
}

esp_err_t sys_esp_rmaker_param_update_and_notify(const esp_rmaker_param_t *param, esp_rmaker_param_val_t *val)
{
    if (!is_valid_user_d_addr((void *)val)) {
        return ESP_ERR_INVALID_ARG;
    }
    int wrapper_index = (int)param;
    esp_map_handle_t *wrapper_handle = esp_map_verify(wrapper_index, ESP_MAP_RMAKER_PARAM_ID);
    if (!wrapper_handle) {
        return ESP_ERR_INVALID_ARG;
    }
    return esp_rmaker_param_update_and_notify((const esp_rmaker_param_t *)wrapper_handle->handle, *val);
}

esp_err_t sys_esp_rmaker_param_update_and_report(const esp_rmaker_param_t *param, esp_rmaker_param_val_t *val)
{
    if (!is_valid_user_d_addr((void *)val)) {
        return ESP_ERR_INVALID_ARG;
    }
    int wrapper_index = (int)param;
    esp_map_handle_t *wrapper_handle = esp_map_verify(wrapper_index, ESP_MAP_RMAKER_PARAM_ID);
    if (!wrapper_handle) {
        return ESP_ERR_INVALID_ARG;
    }
    return esp_rmaker_param_update_and_report((const esp_rmaker_param_t *)wrapper_handle->handle, *val);
}

esp_rmaker_param_t *sys_esp_rmaker_device_get_param_by_name(const esp_rmaker_device_t *device, const char *param_name, int param_len)
{
    if (!is_valid_user_d_addr((void *)param_name) ||
        !is_valid_user_d_addr((void *)(param_name + param_len)) ||
        *(param_name + param_len) != '\0') {
        return NULL;
    }
    int wrapper_index = (int)device;
    esp_map_handle_t *wrapper_handle = esp_map_verify(wrapper_index, ESP_MAP_RMAKER_DEVICE_ID);
    if (!wrapper_handle) {
        return NULL;
    }
    esp_rmaker_param_t *param = esp_rmaker_device_get_param_by_name((const esp_rmaker_device_t *)wrapper_handle->handle, param_name);
    int allocated_handles = esp_map_get_allocated_size();
    for (int i = ESP_MAP_INDEX_OFFSET; i < (ESP_MAP_INDEX_OFFSET + allocated_handles); i++) {
        esp_map_handle_t *wrapper_handle = esp_map_get_handle(i);
        if (!wrapper_handle) {
            continue;
        }
        if (wrapper_handle->handle == param) {
            return (esp_rmaker_param_t *)i;
        }
    }
    return NULL;
}

esp_rmaker_param_t *sys_esp_rmaker_device_get_param_by_type(const esp_rmaker_device_t *device, const char *param_type, int type_len)
{
    if (!is_valid_user_d_addr((void *)param_type) ||
        !is_valid_user_d_addr((void *)(param_type + type_len)) ||
        *(param_type + type_len) != '\0') {
        return NULL;
    }
    int wrapper_index = (int)device;
    esp_map_handle_t *wrapper_handle = esp_map_verify(wrapper_index, ESP_MAP_RMAKER_DEVICE_ID);
    if (!wrapper_handle) {
        return NULL;
    }
    esp_rmaker_param_t *param = esp_rmaker_device_get_param_by_type((const esp_rmaker_device_t *)wrapper_handle->handle, param_type);

    int user_handles = esp_map_get_allocated_size();
    for (int i = ESP_MAP_INDEX_OFFSET; i < (ESP_MAP_INDEX_OFFSET + user_handles); i++) {
        esp_map_handle_t *param_handle = esp_map_get_handle(i);
        if (!param_handle) {
            continue;
        }
        if (param_handle->handle == param) {
            return (esp_rmaker_param_t *)i;
        }
    }
    return NULL;
}

esp_err_t sys_esp_rmaker_param_add_ui_type(const esp_rmaker_param_t *param, const char *ui_type, int ui_len)
{
    if (!is_valid_user_d_addr((void *)ui_type) ||
        !is_valid_user_d_addr((void *)(ui_type + ui_len)) ||
        *(ui_type + ui_len) != '\0') {
        return ESP_ERR_INVALID_ARG;
    }
    int wrapper_index = (int)param;
    esp_map_handle_t *wrapper_handle = esp_map_verify(wrapper_index, ESP_MAP_RMAKER_PARAM_ID);
    if (!wrapper_handle) {
        return ESP_ERR_INVALID_ARG;
    }
    return esp_rmaker_param_add_ui_type((const esp_rmaker_param_t *)wrapper_handle->handle, ui_type);
}

esp_err_t sys_esp_rmaker_param_add_bounds(const esp_rmaker_param_t *param,
        esp_rmaker_param_val_t *min, esp_rmaker_param_val_t *max, esp_rmaker_param_val_t *step)
{
    if (!is_valid_user_d_addr((void *)min) || !is_valid_user_d_addr((void *)max) ||
            !is_valid_user_d_addr((void *)step)) {
        return ESP_ERR_INVALID_ARG;
    }
    int wrapper_index = (int)param;
    esp_map_handle_t *wrapper_handle = esp_map_verify(wrapper_index, ESP_MAP_RMAKER_PARAM_ID);
    if (!wrapper_handle) {
        return ESP_ERR_INVALID_ARG;
    }
    return esp_rmaker_param_add_bounds((const esp_rmaker_param_t *)wrapper_handle->handle, *min, *max, *step);
}

esp_err_t sys_esp_rmaker_param_get_name(const esp_rmaker_param_t *param, char *buffer, int buffer_len)
{
    if (!is_valid_udram_addr((void *)buffer) || !is_valid_udram_addr((void *)(buffer + buffer_len))) {
        return ESP_ERR_INVALID_ARG;
    }
    int wrapper_index = (int)param;
    if (!is_valid_udram_addr(buffer) || !is_valid_user_d_addr(buffer + buffer_len)) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_map_handle_t *wrapper_handle = esp_map_verify(wrapper_index, ESP_MAP_RMAKER_PARAM_ID);
    if (!wrapper_handle) {
        return ESP_ERR_INVALID_ARG;
    }
    char *param_name = esp_rmaker_param_get_name((const esp_rmaker_param_t *)wrapper_handle->handle);
    strlcpy(buffer, param_name, buffer_len);
    return ESP_OK;
}

button_handle_t sys_iot_button_create(gpio_num_t gpio_num, button_active_t active_level)
{
    button_handle_t button = iot_button_create(gpio_num, active_level);
    if (!button) {
        return NULL;
    }
    int wrapper_index = esp_map_add(button, ESP_MAP_RMAKER_BUTTON_ID);
    if (!wrapper_index) {
        iot_button_delete(button);
        return NULL;
    }
    return (button_handle_t)wrapper_index;
}

void sys_button_cb(void *arg)
{
    if (!sys_rmaker_dispatcher_queue) {
        return;
    }

    usr_rmaker_dispatch_ctx_t dispatch_ctx = {
        .event = ESP_SYSCALL_EVENT_BUTTON,
    };
    memcpy(&dispatch_ctx.dispatch_data.button_cb_args, arg, sizeof(usr_button_cb_args_t));
    if (xQueueSend(sys_rmaker_dispatcher_queue, &dispatch_ctx, portMAX_DELAY) == pdFALSE) {
        ESP_LOGE(TAG, "Error sending message on queue");
    }
}

esp_err_t sys_iot_button_set_evt_cb(button_handle_t btn_handle, button_cb_type_t type, button_cb cb, void* arg)
{
    if (is_valid_kernel_d_addr(arg)) {
        return ESP_ERR_INVALID_ARG;
    }
    int wrapper_index = (int)btn_handle;
    esp_map_handle_t *wrapper_handle = esp_map_verify(wrapper_index, ESP_MAP_RMAKER_BUTTON_ID);
    if (!wrapper_handle) {
        return ESP_ERR_INVALID_ARG;
    }
    usr_button_cb_args_t *usr_context = heap_caps_malloc(sizeof(usr_button_cb_args_t), MALLOC_CAP_DEFAULT | MALLOC_CAP_INTERNAL);
    if (!usr_context) {
        ESP_LOGE(TAG, "Insufficient memory for user context");
        return ESP_FAIL;
    }
    usr_context->usr_cb = cb;
    usr_context->usr_args = arg;
    return iot_button_set_evt_cb((button_handle_t)wrapper_handle->handle, type, sys_button_cb, usr_context);
}

esp_err_t sys_app_reset_button_register(button_handle_t btn_handle, uint8_t wifi_reset_timeout,
        uint8_t factory_reset_timeout)
{
    int wrapper_index = (int)btn_handle;
    esp_map_handle_t *wrapper_handle = esp_map_verify(wrapper_index, ESP_MAP_RMAKER_BUTTON_ID);
    if (!wrapper_handle) {
        return ESP_ERR_INVALID_ARG;
    }
    return app_reset_button_register((button_handle_t)wrapper_handle->handle, wifi_reset_timeout, factory_reset_timeout);
}

esp_err_t sys_ws2812_led_init(void)
{
    return ws2812_led_init();
}

esp_err_t sys_ws2812_led_set_rgb(uint32_t red, uint32_t green, uint32_t blue)
{
    return ws2812_led_set_rgb(red, green, blue);
}

esp_err_t sys_ws2812_led_set_hsv(uint32_t hue, uint32_t saturation, uint32_t value)
{
    return ws2812_led_set_hsv(hue, saturation, value);
}

esp_err_t sys_ws2812_led_clear(void)
{
    return ws2812_led_clear();
}

esp_err_t sys_app_wifi_start(app_wifi_pop_type_t pop_type)
{
    return app_wifi_start(pop_type);
}

/* Event handler for catching RainMaker events */
static void sys_event_handler(void* arg, esp_event_base_t event_base,
                          int32_t event_id, void* event_data)
{
    if (event_base == RMAKER_EVENT) {
        switch (event_id) {
            case RMAKER_EVENT_INIT_DONE:
                ESP_LOGI(TAG, "RainMaker Initialised.");
                break;
            case RMAKER_EVENT_CLAIM_STARTED:
                ESP_LOGI(TAG, "RainMaker Claim Started.");
                break;
            case RMAKER_EVENT_CLAIM_SUCCESSFUL:
                ESP_LOGI(TAG, "RainMaker Claim Successful.");
                break;
            case RMAKER_EVENT_CLAIM_FAILED:
                ESP_LOGI(TAG, "RainMaker Claim Failed.");
                break;
            default:
                ESP_LOGW(TAG, "Unhandled RainMaker Event: %d", event_id);
        }
    } else if (event_base == RMAKER_COMMON_EVENT) {
        switch (event_id) {
            case RMAKER_EVENT_REBOOT:
                ESP_LOGI(TAG, "Rebooting in %d seconds.", *((uint8_t *)event_data));
                break;
            case RMAKER_EVENT_WIFI_RESET:
                ESP_LOGI(TAG, "Wi-Fi credentials reset.");
                break;
            case RMAKER_EVENT_FACTORY_RESET:
                ESP_LOGI(TAG, "Node reset to factory defaults.");
                break;
            case RMAKER_MQTT_EVENT_CONNECTED:
                ESP_LOGI(TAG, "MQTT Connected.");
                break;
            case RMAKER_MQTT_EVENT_DISCONNECTED:
                ESP_LOGI(TAG, "MQTT Disconnected.");
                break;
            case RMAKER_MQTT_EVENT_PUBLISHED:
                ESP_LOGI(TAG, "MQTT Published. Msg id: %d.", *((int *)event_data));
                break;
            default:
                ESP_LOGW(TAG, "Unhandled RainMaker Common Event: %d", event_id);
        }
    } else {
        ESP_LOGW(TAG, "Invalid event received!");
    }
}

esp_err_t sys_esp_rmaker_kernel_init(void)
{
    /* Initialize Application specific hardware drivers and
     * set initial state.
     */

    /* Initialize NVS. */
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialise NVS flash, 0x%x", err);
        return err;
    }

    /* Initialize Wi-Fi. Note that, this should be called before esp_rmaker_node_init()
     */
    app_wifi_init();

    /* Register an event handler to catch RainMaker events */
    err = esp_event_handler_register(RMAKER_EVENT, ESP_EVENT_ANY_ID, &sys_event_handler, NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to regieter event handler for RMAKER_EVENT, 0x%x", err);
        return err;
    }
    return ESP_OK;
}
