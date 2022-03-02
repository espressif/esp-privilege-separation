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

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "driver/spi_common.h"
#include "driver/spi_master.h"

#define IOCTL_NUM(type, num)      (type|num)

#define SPI_BASE                0x2000
#define SPI_INIT                IOCTL_NUM(SPI_BASE, 0)
#define SPI_DEINIT              IOCTL_NUM(SPI_BASE, 1)
#define SPI_MASTER_ADD_DEV      IOCTL_NUM(SPI_BASE, 2)
#define SPI_MASTER_REMOVE_DEV   IOCTL_NUM(SPI_BASE, 3)

/**
* @brief Register SPI device in VFS with the given pathname
*
* @param path: Path with which the device should be addressed
*
* @return:
*   - ESP_OK: Device registered successfully
*   - otherwise see esp_err_t
*/
esp_err_t esp_spi_device_register(const char* path);

#ifdef __cplusplus
}
#endif

