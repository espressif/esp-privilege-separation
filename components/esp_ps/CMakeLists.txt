if(USER_APP_BUILD)
    set(srcs)
else()
    set(srcs "esp_ps_image_utility.c"
             "esp_ps_setup.c"
             "esp_ps_panic.c")
endif()

idf_component_register(SRCS ${srcs}
                    INCLUDE_DIRS "include"
                    REQUIRES bootloader_support spi_flash app_update)
