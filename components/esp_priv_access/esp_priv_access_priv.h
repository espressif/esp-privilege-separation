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

#include "esp_log.h"
#include <esp_image_format.h>
#include "soc_defs.h"

esp_err_t esp_priv_access_user_unpack(esp_image_metadata_t *user_img_data);

esp_err_t esp_priv_access_load_user_app_desc(usr_custom_app_desc_t *app_desc);

void esp_priv_access_handle_crashed_task(void);
