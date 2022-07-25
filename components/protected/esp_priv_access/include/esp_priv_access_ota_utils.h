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

#include <esp_err.h>
#include <esp_partition.h>

// User otadata magic is derived from sha256 of "user_ota" string
#define USER_OTADATA_MAGIC 0xbde55c5e

typedef struct {
    uint32_t magic;
    uint8_t version;
    uint8_t boot_partition;
    uint8_t try_partition;
    uint8_t reserved_1;
    uint32_t reserved_2;
    uint32_t crc;
} esp_pa_ota_select_entry_t;

const esp_partition_t *esp_priv_access_get_boot_partition(void);
const esp_partition_t *esp_priv_access_get_next_update_partition(void);
esp_err_t esp_priv_access_set_boot_partition(const esp_partition_t *partition);
#ifdef CONFIG_PA_ENABLE_USER_APP_ROLLBACK
esp_err_t esp_priv_access_mark_user_app_valid_and_cancel_rollback(void);
#endif

