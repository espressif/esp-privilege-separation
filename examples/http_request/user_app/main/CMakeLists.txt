set(includes
   $ENV{IDF_PATH}/components/esp_common/include
   $ENV{IDF_PATH}/components/soc/include
   $ENV{IDF_PATH}/components/esp_rom/include
   $ENV{IDF_PATH}/components/esp_timer/include
   $ENV{IDF_PATH}/components/esp32c3/include
   $ENV{IDF_PATH}/components/riscv/include
   $ENV{IDF_PATH}/components/freertos/include
   $ENV{IDF_PATH}/components/freertos/port/riscv/include
   $ENV{IDF_PATH}/components/esp_event/include
   $ENV{IDF_PATH}/components/esp_wifi/include
   $ENV{IDF_PATH}/components/driver/include
   $ENV{IDF_PATH}/components/tcpip_adapter/include
   $ENV{IDF_PATH}/components/esp-tls
   $ENV{IDF_PATH}/components/nvs_flash/include
   $ENV{IDF_PATH}/components/spi_flash/include/
   ${PROJECT_DIR}/
   ${PROJECT_DIR}/../../../components/esp_syscall/include
   ${PROJECT_DIR}/../../../components/esp_ps/include
   .
    )

set(private_include_dirs
    $ENV{IDF_PATH}/components/hal/include
    $ENV{IDF_PATH}/components/hal/esp32c3/include/
    $ENV{IDF_PATH}/components/freertos/include/freertos
    $ENV{IDF_PATH}/components/freertos/port/riscv/include/freertos
    $ENV{IDF_PATH}/components/freertos/port/riscv
    $ENV{IDF_PATH}/components/riscv/include
    $ENV{IDF_PATH}/components/soc/include
    $ENV{IDF_PATH}/components/soc/esp32c3/include
    $ENV{IDF_PATH}/components/heap/include
    $ENV{IDF_PATH}/components/esp_system/include
    $ENV{IDF_PATH}/components/esp_netif/include
    $ENV{IDF_PATH}/components/esp_eth/include
    $ENV{IDF_PATH}/components/esp_hw_support/include
    $ENV{IDF_PATH}/components/lwip/lwip/src/include
    $ENV{IDF_PATH}/components/lwip/port/esp32/include
    $ENV{IDF_PATH}/components/newlib/platform_include
    $ENV{IDF_PATH}/components/lwip/include
    $ENV{IDF_PATH}/components/lwip/include/apps/sntp/
    $ENV{IDF_PATH}/components/lwip/include/apps/
    $ENV{IDF_PATH}/components/mbedtls/mbedtls/include/
    $ENV{IDF_PATH}/components/mbedtls/port/include/
    )

idf_component_register(SRCS "user_code.c"
                    INCLUDE_DIRS ${includes}
                    PRIV_INCLUDE_DIRS ${private_include_dirs})

set_property(TARGET ${COMPONENT_LIB} APPEND PROPERTY INTERFACE_LINK_LIBRARIES "-Wl,--gc-sections")

target_linker_script(${COMPONENT_LIB} INTERFACE $ENV{IDF_PATH}/components/esp32c3/ld/esp32c3.peripherals.ld)

target_linker_script(${COMPONENT_LIB} INTERFACE "${CMAKE_CURRENT_BINARY_DIR}/user_out.ld")

set(LD_DIR ${PROJECT_DIR}/ld)

add_custom_command(
    OUTPUT user_out.ld
    COMMAND "${CMAKE_C_COMPILER}" -C -P -x c -E -o user_out.ld -I ${PROJECT_DIR} ${LD_DIR}/user.ld
    MAIN_DEPENDENCY ${LD_DIR}/user.ld ${PROJECT_DIR}/user_config.h
    COMMENT "Generating linker script..."
    VERBATIM)

add_custom_target(user_linker_script DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/user_out.ld)
add_dependencies(${COMPONENT_LIB} user_linker_script)

target_link_libraries(${COMPONENT_LIB} INTERFACE "-u _user_main")
target_link_libraries(${COMPONENT_LIB} INTERFACE "-u user_main")
target_link_libraries(${COMPONENT_LIB} INTERFACE "-u user_app_desc")
target_link_libraries(${COMPONENT_LIB} INTERFACE "-Wl,--wrap=printf -Wl,--defsym=__wrap_printf=usr_printf")
target_link_libraries(${COMPONENT_LIB} INTERFACE "${DEFSYM_LIST}")

add_prebuilt_library(esp_syscall ${SYSCALL_LIB})
target_link_libraries(${COMPONENT_LIB} PUBLIC esp_syscall)
