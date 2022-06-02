if(USER_APP_BUILD)
    set(srcs)
else()
    set(srcs "esp_priv_access_image_utility.c"
             "esp_priv_access_setup.c"
             "esp_priv_access_panic.c"
             "esp_priv_access_ota_utils.c")
endif()

idf_component_register(SRCS ${srcs}
                    INCLUDE_DIRS "include"
                    PRIV_INCLUDE_DIRS "."
                    PRIV_REQUIRES bootloader_support spi_flash app_update console esp_priv_build_utils)
