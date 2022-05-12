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

/*
 * esp_event library uses pointers to compare event base.
 * This behaviour creates an issue when event base is sent
 * from protected app to user app. This is a custom source
 * file that defines event bases for user app.
 */
#include "esp_event.h"

ESP_EVENT_DEFINE_BASE(WIFI_EVENT);

ESP_EVENT_DEFINE_BASE(IP_EVENT);

