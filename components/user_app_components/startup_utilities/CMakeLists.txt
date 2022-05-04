set(srcs "startup_utilities.c")

idf_component_register(SRCS ${srcs}
                    PRIV_INCLUDE_DIRS "."
                    REQUIRES bootloader_support spi_flash app_update console esp_priv_access syscall_wrapper)
