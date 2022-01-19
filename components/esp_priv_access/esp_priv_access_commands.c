// Copyright 2020 Espressif Systems (Shanghai) PTE LTD
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

#include <esp_priv_access_console.h>

static const char *TAG = "esp_priv_access_console";

static int protected_mem_dump_cli_handler(int argc, char *argv[])
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

static int user_mem_dump_cli_handler(int argc, char *argv[])
{
    printf("\tDescription\tInternal\n");
    printf("Current Free Memory\t%d\n",
           heap_caps_get_free_size(MALLOC_CAP_WORLD1));
    printf("Largest Free Block\t%d\n",
           heap_caps_get_largest_free_block(MALLOC_CAP_WORLD1));
    printf("Min. Ever Free Size\t%d\n",
           heap_caps_get_minimum_free_size(MALLOC_CAP_WORLD1));
    return 0;
}

static int protected_task_dump_cli_handler(int argc, char *argv[])
{
#ifndef CONFIG_FREERTOS_USE_TRACE_FACILITY
    printf("%s: To use this utility enable: Component config --> FreeRTOS --> Enable FreeRTOS trace facility\n", TAG);
#else
    int num_of_tasks = uxTaskGetNumberOfTasks();
    TaskStatus_t *task_array = calloc(num_of_tasks, sizeof(TaskStatus_t));
    if (!task_array) {
        ESP_LOGE(TAG, "Memory allocation for task list failed.");
        return -1;
    }
    num_of_tasks = uxTaskGetSystemState(task_array, num_of_tasks, NULL);
    printf("%s: \tName\tNumber\tPriority\tStackWaterMark\n", TAG);
    for (int i = 0; i < num_of_tasks; i++) {
        if (pvTaskGetThreadLocalStoragePointer(task_array[i].xHandle, 1) != NULL) {
            continue;
        }
        printf("%36s\t%d\t%d\t%12d\n",
               task_array[i].pcTaskName,
               task_array[i].xTaskNumber,
               task_array[i].uxCurrentPriority,
               task_array[i].usStackHighWaterMark);
    }
    free(task_array);
#endif
    return 0;
}

static int user_task_dump_cli_handler(int argc, char *argv[])
{
#ifndef CONFIG_FREERTOS_USE_TRACE_FACILITY
    printf("%s: To use this utility enable: Component config --> FreeRTOS --> Enable FreeRTOS trace facility\n", TAG);
#else
    int num_of_tasks = uxTaskGetNumberOfTasks();
    TaskStatus_t *task_array = calloc(num_of_tasks, sizeof(TaskStatus_t));
    if (!task_array) {
        ESP_LOGE(TAG, "Memory allocation for task list failed.");
        return -1;
    }
    num_of_tasks = uxTaskGetSystemState(task_array, num_of_tasks, NULL);
    printf("%s: \tName\tNumber\tPriority\tStackWaterMark\n", TAG);
    for (int i = 0; i < num_of_tasks; i++) {
        if (pvTaskGetThreadLocalStoragePointer(task_array[i].xHandle, 1) == NULL) {
            continue;
        }
        printf("%36s\t%d\t%d\t%12d\n",
               task_array[i].pcTaskName,
               task_array[i].xTaskNumber,
               task_array[i].uxCurrentPriority,
               task_array[i].usStackHighWaterMark);
    }
    free(task_array);
#endif
    return 0;
}

static int cpu_dump_cli_handler(int argc, char *argv[])
{
#ifndef CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS
    printf("%s: To use this utility enable: Component config --> FreeRTOS --> Enable FreeRTOS to collect run time stats\n", TAG);
#else
    char *buf = calloc(1, 2 * 1024);
    if (!buf) {
        ESP_LOGE(TAG, "Memory allocation for cpu dump failed.");
        return -1;
    }
    vTaskGetRunTimeStats(buf);
    printf("%s: Run Time Stats:\n%s\n", TAG, buf);
    free(buf);
#endif
    return 0;
}

static void default_commands(void)
{
    const esp_console_cmd_t debug_commands[] = {
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
        {
            .command = "protected-task-dump",
            .help = "Get the list of all the protected tasks.",
            .func = protected_task_dump_cli_handler,
        },
        {
            .command = "user-task-dump",
            .help = "Get the list of all the user tasks.",
            .func = user_task_dump_cli_handler,
        },
        {
            .command = "cpu-dump",
            .help = "Get the CPU utilisation by all the runninng tasks.",
            .func = cpu_dump_cli_handler,
        },
    };

    int cmds_num = sizeof(debug_commands) / sizeof(esp_console_cmd_t);
    int i;
    for (i = 0; i < cmds_num; i++) {
        ESP_LOGI(TAG, "Registering command: %s", debug_commands[i].command);
        esp_console_cmd_register(&debug_commands[i]);
    }
}

void register_commands(void)
{
    default_commands();
}
