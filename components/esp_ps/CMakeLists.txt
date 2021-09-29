set(srcs "esp_ps_image_utility.c"
         "esp_ps_setup.c")

idf_component_register(SRCS ${srcs}
                    INCLUDE_DIRS "include"
                    REQUIRES bootloader_support spi_flash)
