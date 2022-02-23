idf_component_register(SRCS esp_user_ota.c
                       INCLUDE_DIRS .
                       PRIV_REQUIRES app_update esp_http_client mbedtls console esp_priv_access
                       )
