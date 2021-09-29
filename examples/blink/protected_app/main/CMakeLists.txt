set(src "protected_main.c")

idf_component_register(SRCS ${src}
                       INCLUDE_DIRS "."
                       REQUIRES bootloader_support spi_flash esp_ps)

target_linker_script(${COMPONENT_LIB} INTERFACE ${COMPONENT_DIR}/../ld/reserve.ld)
