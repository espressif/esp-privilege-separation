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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "syscall_wrappers.h"

#include "soc/gpio_struct.h"
#include "hal/gpio_types.h"
#include "driver/gpio.h"
#include "hal/gpio_ll.h"
#include "ws2812.h"

#include "esp_log.h"

#if CONFIG_IDF_TARGET_ESP32C3
#define WS2812_GPIO     8
#define BUTTON_IO       9
#elif CONFIG_IDF_TARGET_ESP32S3
#define WS2812_GPIO    48
#define BUTTON_IO       0
#endif
#define INTR_LED        2
#define BLINK_GPIO      10

static int g_state = 0;
static usr_gpio_handle_t intr_gpio_handle;

static const char *TAG = "user_main";

/* User space code is never executed in ISR context,
 * so user registered "ISR" functions are actually executed
 * from task's context, hence they are termed as softISRs
 */
UIRAM_ATTR void user_gpio_softisr(void *arg)
{
    int gpio_num = (int)arg;
    if (g_state == 1) {
        gpio_ll_set_level(&GPIO, gpio_num, 0);
        g_state = 0;
    } else {
        gpio_ll_set_level(&GPIO, gpio_num, 1);
        g_state = 1;
    }
}

void blink_task()
{
    /* WS2812 LED expects data in multiple of 3. 3 bytes for 1 LED
     * The data format is {R, G, B}, with intensity ranging from 0 - 255.
     * 0 being dimmest (off) and 255 being the brightest
     */
    uint8_t data_on[3] = {0, 8, 8};
    uint8_t data_off[3] = {0, 0, 0};

    ws2812_dev_conf_t dev_cnf = {
        .channel = 0,
        .gpio_num = WS2812_GPIO,
        .led_cnt = 1
    };

    int ws2812_fd = open("/dev/ws2812/0", O_WRONLY);

    ioctl(ws2812_fd, WS2812_INIT, &dev_cnf);

    while (1) {
        gpio_ll_set_level(&GPIO, BLINK_GPIO, 1);
        write(ws2812_fd, data_on, 3);
        vTaskDelay(100);

        gpio_ll_set_level(&GPIO, BLINK_GPIO, 0);
        write(ws2812_fd, data_off, 3);
        vTaskDelay(100);
    }
}

void user_main()
{
    gpio_config_t io_conf;
    io_conf.pin_bit_mask = (1 << BLINK_GPIO);
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.intr_type = GPIO_PIN_INTR_DISABLE;
    io_conf.pull_down_en = 0;
    io_conf.pull_up_en = 0;
    gpio_config(&io_conf);

    io_conf.pin_bit_mask = (1 << INTR_LED);
    gpio_config(&io_conf);

    io_conf.pin_bit_mask = (1 << BUTTON_IO);
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.intr_type = GPIO_INTR_NEGEDGE;
    io_conf.pull_up_en = 1;
    gpio_config(&io_conf);

    gpio_install_isr_service(0);
    gpio_softisr_handler_add(BUTTON_IO, user_gpio_softisr, (void*)INTR_LED, &intr_gpio_handle);

    if (xTaskCreate(blink_task, "Blink task", 4096, NULL, 1, NULL) != pdPASS) {
        ESP_LOGE(TAG, "Task Creation failed");
    }
}
