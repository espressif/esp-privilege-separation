idf_component_register(SRCS "user_code.c"
                    INCLUDE_DIRS ".."
                    EMBED_TXTFILES server_root_cert.pem)

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
target_link_libraries(${COMPONENT_LIB} INTERFACE "-Wl,--wrap=memset -Wl,--defsym=__wrap_memset=usr_memset")
target_link_libraries(${COMPONENT_LIB} INTERFACE "-Wl,--wrap=strlen -Wl,--defsym=__wrap_strlen=usr_strlen")
target_link_libraries(${COMPONENT_LIB} INTERFACE "${DEFSYM_LIST}")

add_prebuilt_library(esp_syscall ${SYSCALL_LIB})
target_link_libraries(${COMPONENT_LIB} PUBLIC esp_syscall)
