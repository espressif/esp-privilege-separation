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

#include "esp_vfs.h"
#include "esp_vfs_dev.h"
#include "esp_err.h"
#include "esp_log.h"
#include <errno.h>
#include <sys/fcntl.h>
#include <sys/errno.h>
#include <sys/unistd.h>
#include "led_strip.h"
#include "ws2812.h"

#define TAG                   "ws2812"

#define WS2812_FD_0           32
#define WS2812_FD_1           33

static led_strip_t *dev0;
static led_strip_t *dev1;

static int ws2812_open(const char * path, int flags, int mode)
{
    int fd = -1;

    if (strcmp(path, "/0") == 0) {
        fd = WS2812_FD_0;
    } else if ((strcmp(path, "/1") == 0)) {
        fd = WS2812_FD_1;
    } else {
        errno = ENOENT;
        return fd;
    }

    return fd;
}

static ssize_t ws2812_write(int fd, const void * data, size_t size)
{
    assert(fd == WS2812_FD_0 || fd == WS2812_FD_1);

    led_strip_t **dev = (fd == WS2812_FD_0 ? &dev0 : &dev1);

    if (*dev == NULL) {
        ESP_LOGE(TAG, "Device not initialized");
        return -1;
    }

    if (size % 3) {
        ESP_LOGE(TAG, "Data has to be in multiple of 3 (3 bytes per LED)");
        return -1;
    }

    uint8_t *data_val = (uint8_t *)data;

    for (int i = 0; i < size; i+=3) {
        (*dev)->set_pixel(*dev, i / 3, data_val[i], data_val[i+1], data_val[i+2]);
    }

    (*dev)->refresh(*dev, 100);

    return size;
}

static int ws2812_close(int fd)
{
    assert(fd == WS2812_FD_0 || fd == WS2812_FD_1);
    return 0;
}

static int ws2812_ioctl(int fd, int cmd, va_list arg)
{
    assert(fd == WS2812_FD_0 || fd == WS2812_FD_1);

    led_strip_t **dev = (fd == WS2812_FD_0 ? &dev0 : &dev1);

    switch(cmd) {
        case WS2812_INIT: {
            if (*dev) {
                ESP_LOGE(TAG, "Device already initialized");
                return -1;
            }
            ws2812_dev_conf_t *dev_conf = va_arg(arg, ws2812_dev_conf_t *);
            *dev = led_strip_init(dev_conf->channel, dev_conf->gpio_num, dev_conf->led_cnt);
            if (*dev == NULL) {
                errno = EIO;
                return -1;
            }
            break;
        }
        case WS2812_DEINIT: {
            if (*dev == NULL) {
                ESP_LOGE(TAG, "Device not initialized");
                return -1;
            }
            led_strip_deinit(*dev);
            *dev = NULL;
            break;
        }
        default:
            ESP_LOGE(TAG, "Invalid command");
            return -1;
    }

    return 0;
}

static const esp_vfs_t vfs = {
    .open = &ws2812_open,
    .write = &ws2812_write,
    .close = &ws2812_close,
    .ioctl = &ws2812_ioctl,
};

esp_err_t esp_ws2812_device_register(const char* path)
{
    return esp_vfs_register(path, &vfs, NULL);
}
