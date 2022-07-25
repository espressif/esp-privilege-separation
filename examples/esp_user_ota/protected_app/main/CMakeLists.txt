set(src "protected_main.c")

idf_component_register(SRCS ${src}
                       INCLUDE_DIRS "."
                       REQUIRES bootloader_support spi_flash esp_priv_access)
