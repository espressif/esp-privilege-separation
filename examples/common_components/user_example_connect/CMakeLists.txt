idf_component_register(SRCS user_example_connect.c
                        INCLUDE_DIRS .
                        PRIV_REQUIRES esp_wifi esp_netif nvs_flash esp_priv_build_utils syscall_shared syscall_wrapper)

if(NOT USER_APP_BUILD)
    message(FATAL_ERROR "user_example_connect should be included in user app build only.")
endif()
