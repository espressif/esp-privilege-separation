set(src "protected_main.c")

idf_component_register(SRCS ${src}
                       INCLUDE_DIRS "."
                       REQUIRES bootloader_support spi_flash esp_ps)

target_linker_script(${COMPONENT_LIB} INTERFACE "${CMAKE_CURRENT_BINARY_DIR}/reserve_out.ld")

set(LD_DIR ${PROJECT_DIR}/protected_app/ld)

add_custom_command(
    OUTPUT reserve_out.ld
    COMMAND "${CMAKE_C_COMPILER}" -C -P -x c -E -o reserve_out.ld -I ${CONFIG_DIR} ${LD_DIR}/reserve.ld
    MAIN_DEPENDENCY ${LD_DIR}/reserve.ld ${CONFIG_DIR}/sdkconfig.h
    COMMENT "Generating reserve linker script..."
    VERBATIM)

add_custom_target(reserve_linker_script DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/reserve_out.ld)
add_dependencies(${COMPONENT_LIB} reserve_linker_script)
