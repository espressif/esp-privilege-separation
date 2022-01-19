#pragma once
#include <esp_err.h>

esp_err_t esp_priv_access_console_init();
void register_commands(void);
