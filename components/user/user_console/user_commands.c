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
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <esp_log.h>
#include <esp_console.h>
#include <esp_heap_caps.h>

#include <user_console.h>
#include <syscall_structs.h>

static const char *TAG = "user_console";

esp_err_t usr_esp_get_protected_heap_stats(protected_heap_stats_t *);

static int protected_mem_dump_cli_handler(int argc, char *argv[])
{
    protected_heap_stats_t stats = {0};
    usr_esp_get_protected_heap_stats(&stats);
    printf("\tDescription\tInternal\n");
    printf("Current Free Memory\t%d\n",
            stats.free_heap_size);
    printf("Largest Free Block\t%d\n",
            stats.largest_free_block);
    printf("Min. Ever Free Size\t%d\n",
            stats.min_free_heap);
    return 0;
}

static int user_mem_dump_cli_handler(int argc, char *argv[])
{
    printf("\tDescription\tInternal\n");
    printf("Current Free Memory\t%d\n",
           heap_caps_get_free_size(MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL));
    printf("Largest Free Block\t%d\n",
           heap_caps_get_largest_free_block(MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL));
    printf("Min. Ever Free Size\t%d\n",
           heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL));
    return 0;
}

static const esp_console_cmd_t debug_commands[] = {
    {
        .command = "protected-mem-dump",
        .help = "Get the available memory for protected app.",
        .func = protected_mem_dump_cli_handler,
    },
    {
        .command = "user-mem-dump",
        .help = "Get the available memory for user app.",
        .func = user_mem_dump_cli_handler,
    },
};

static int help_command(int argc, char *argv[])
{
    for (int i = 0; i < sizeof(debug_commands)/sizeof(debug_commands[0]); i++) {
        if (debug_commands[i].help == NULL) {
            continue;
        }
        const char *hint = (debug_commands[i].hint) ? debug_commands[i].hint : "";
        printf("%-s %s\n", debug_commands[i].command, hint);
        printf("  ");
        printf("%s\n", debug_commands[i].help);
    }
    return 0;
}

static void default_commands(void)
{
    int cmds_num = sizeof(debug_commands) / sizeof(esp_console_cmd_t);
    int i;
    for (i = 0; i < cmds_num; i++) {
        ESP_LOGI(TAG, "Registering command: %s", debug_commands[i].command);
        esp_console_cmd_register(&debug_commands[i]);
    }
    const esp_console_cmd_t help_cmd = {
        .command = "help",
        .help = "Print the list of registered commands",
        .func = &help_command,
    };
    esp_console_cmd_register(&help_cmd);
}

void register_commands(void)
{
    default_commands();
}
