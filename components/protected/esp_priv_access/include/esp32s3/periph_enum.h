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

// Do NOT change this enum.
// The values assigned are not random. It should be consistent
// with permc_pif_t enum

typedef enum {
    PA_UART1 = 0,
    PA_I2S0,
    PA_I2C,
    PA_MISC,
    PA_HINF = 5,
    PA_IO_MUX = 7,
    PA_RTC,
    PA_FE = 10,
    PA_FE2 = 11,
    PA_GPIO,
    PA_G0SPI_0,
    PA_G0SPI_1,
    PA_UART,
    PA_SYSTIMER,
    PA_TIMERGROUP1,
    PA_TIMERGROUP,
    PA_PWM0,
    PA_BB,
    PA_BACKUP = 22,
    PA_LEDC,
    PA_SLC,
    PA_PCNT,
    PA_RMT,
    PA_SLCHOST,
    PA_UHCI0,
    PA_I2C_EXT0,
    PA_BT = 31,
    PA_PWR = 33,
    PA_WIFIMAC,
    PA_RWBT = 36,
    PA_UART2 = 39,
    PA_I2S1,
    PA_PWM1,
    PA_CAN,
    PA_SDIO_HOST,
    PA_I2C_EXT1,
    PA_APB_CTRL,
    PA_SPI_3,
    PA_SPI_2,
    PA_WORLD_CONTROLLER,
    PA_DIO,
    PA_AD,
    PA_CACHE_CONFIG,
    PA_DMA_COPY,
    PA_INTERRUPT,
    PA_SENSITIVE,
    PA_SYSTEM,
    PA_USB,
    PA_BT_PWR,
    PA_LCD_CAM,
    PA_APB_ADC,
    PA_CRYPTO_DMA,
    PA_CRYPTO_PERI,
    PA_USB_WRAP,
    PA_USB_DEVICE,
    PA_MAX,
} esp_priv_access_periph_t;

#ifdef __cplusplus
}
#endif
