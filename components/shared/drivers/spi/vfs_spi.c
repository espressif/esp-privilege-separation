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

#include "driver/spi_common.h"
#include "driver/spi_master.h"
#include "vfs_spi.h"

#define TAG                   "spi_master"

#define SPI_MASTER_FD_0           34
#define SPI_MASTER_FD_1           35
#define SPI_MASTER_FD_2           36

static bool spi_dev_status[3];
static spi_device_handle_t dev_handle[3];

static int spi_master_open(const char * path, int flags, int mode)
{
    int fd = -1;

    if ((strcmp(path, "/1") == 0)) {
        fd = SPI_MASTER_FD_1;
    } else if ((strcmp(path, "/2") == 0)) {
        fd = SPI_MASTER_FD_2;
    } else {
        errno = ENOENT;
        return fd;
    }

    return fd;
}

static ssize_t spi_master_write(int fd, const void * data, size_t size)
{
    assert(fd == SPI_MASTER_FD_1 || fd == SPI_MASTER_FD_2);
    spi_host_device_t host_id = fd - SPI_MASTER_FD_0;
    spi_transaction_t *trans_desc = (spi_transaction_t *) data;

    if (!spi_dev_status[host_id]) {
        ESP_LOGE(TAG, "SPI bus not initialized");
        return -1;
    }

    esp_err_t err = spi_device_transmit(dev_handle[fd - SPI_MASTER_FD_0], trans_desc);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start transaction");
    }
    return 0;
}

static ssize_t spi_master_read(int fd, void * data, size_t size)
{
    assert(fd == SPI_MASTER_FD_1 || fd == SPI_MASTER_FD_2);
    spi_host_device_t host_id = fd - SPI_MASTER_FD_0;
    spi_transaction_t *trans_desc = (spi_transaction_t *) data;

    if (!spi_dev_status[host_id]) {
        ESP_LOGE(TAG, "SPI bus not initialized");
        return -1;
    }

    esp_err_t err = spi_device_transmit(dev_handle[fd - SPI_MASTER_FD_0], trans_desc);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start transaction");
    }
    return 0;
}

static int spi_master_close(int fd)
{
    assert(fd == SPI_MASTER_FD_1 || fd == SPI_MASTER_FD_2);

    return 0;
}

static int spi_master_ioctl(int fd, int cmd, va_list arg)
{
    assert(fd == SPI_MASTER_FD_1 || fd == SPI_MASTER_FD_2);
    spi_host_device_t host_id = fd - SPI_MASTER_FD_0;

    switch(cmd) {
        case SPI_INIT: {
            if (spi_dev_status[host_id]) {
                ESP_LOGE(TAG, "Device already initialized");
                return -1;
            }
            const spi_bus_config_t *bus_config = va_arg(arg, const spi_bus_config_t *);
            esp_err_t err = spi_bus_initialize(host_id, bus_config, SPI_DMA_CH_AUTO);
            if (err != ESP_OK) {
                errno = EIO;
                return -1;
            }
            spi_dev_status[host_id] = true;
            break;
        }
        case SPI_DEINIT: {
            if (spi_dev_status[host_id] == false) {
                ESP_LOGE(TAG, "Device not initialized");
                return -1;
            }
            spi_bus_free(host_id);
            spi_dev_status[host_id] = false;
            break;
        }
        case SPI_MASTER_ADD_DEV: {
            if (spi_dev_status[host_id] == false) {
                ESP_LOGE(TAG, "Device not initialized");
                return -1;
            }
            const spi_device_interface_config_t *dev_config = va_arg(arg, const spi_device_interface_config_t *);
            spi_bus_add_device(host_id, dev_config, &dev_handle[host_id]);
            break;
        }
        case SPI_MASTER_REMOVE_DEV: {
            if (spi_dev_status[host_id] == false) {
                ESP_LOGE(TAG, "Device not initialized");
                return -1;
            }
            spi_bus_remove_device(dev_handle[host_id]);
            break;
        }
        default:
            ESP_LOGE(TAG, "Invalid command");
            return -1;
    }

    return 0;
}

static const esp_vfs_t vfs = {
    .open = &spi_master_open,
    .write = &spi_master_write,
    .read = &spi_master_read,
    .close = &spi_master_close,
    .ioctl = &spi_master_ioctl,
};

esp_err_t esp_spi_device_register(const char* path)
{
    return esp_vfs_register(path, &vfs, NULL);
}
