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

if (USER_APP_BUILD)

    idf_build_set_property(COMPILE_OPTIONS "-fno-builtin" APPEND)
    idf_build_set_property(COMPILE_OPTIONS "-Wno-format" APPEND)

    target_linker_script(${COMPONENT_LIB} INTERFACE $ENV{IDF_PATH}/components/${IDF_TARGET}/ld/${IDF_TARGET}.peripherals.ld)

    target_linker_script(${COMPONENT_LIB} INTERFACE "${CMAKE_CURRENT_BINARY_DIR}/user_out.ld")

    set(LD_DIR ${COMPONENT_DIR}/ld/user_app/${IDF_TARGET})

    add_custom_command(
        OUTPUT user_out.ld
        COMMAND "${CMAKE_C_COMPILER}" -C -P -x c -E -o user_out.ld -I ${PROTECTED_CONFIG_DIR} -I ${PROJECT_DIR} ${LD_DIR}/user.ld
        MAIN_DEPENDENCY ${LD_DIR}/user.ld ${PROJECT_DIR}/user_config.h
        COMMENT "Generating linker script..."
        VERBATIM)

    add_custom_target(user_linker_script DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/user_out.ld)
    add_dependencies(${COMPONENT_LIB} user_linker_script)

    target_link_libraries(${COMPONENT_LIB} INTERFACE "-u _user_main")
    target_link_libraries(${COMPONENT_LIB} INTERFACE "-u user_main")
    target_link_libraries(${COMPONENT_LIB} INTERFACE "-u user_app_desc")
    target_link_libraries(${COMPONENT_LIB} INTERFACE "-u custom_app_desc")
    target_link_libraries(${COMPONENT_LIB} INTERFACE "${DEFSYM_LIST}")

    add_prebuilt_library(esp_syscall ${SYSCALL_LIB})
    target_link_libraries(${COMPONENT_LIB} INTERFACE esp_syscall)

else()

    target_linker_script(${COMPONENT_LIB} INTERFACE "${CMAKE_CURRENT_BINARY_DIR}/reserve_out.ld")

    set(LD_DIR ${COMPONENT_DIR}/ld/protected_app/${IDF_TARGET})

    add_custom_command(
        OUTPUT reserve_out.ld
        COMMAND "${CMAKE_C_COMPILER}" -C -P -x c -E -o reserve_out.ld -I ${CONFIG_DIR} ${LD_DIR}/reserve.ld
        MAIN_DEPENDENCY ${LD_DIR}/reserve.ld ${CONFIG_DIR}/sdkconfig.h
        COMMENT "Generating reserve linker script..."
        VERBATIM)

    add_custom_target(reserve_linker_script DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/reserve_out.ld)
    add_dependencies(${COMPONENT_LIB} reserve_linker_script)

    # Add user_app binary to the esptool flash arguments
    set(partition user_app)
    set(image_file ${PROJECT_DIR}/build/user_app/user_app.bin)
    partition_table_get_partition_info(offset "--partition-name ${partition}" "offset")
    esptool_py_flash_target_image(flash "${partition}" "${offset}" "${image_file}")
    # Add dependency of idf.py flash on user_app.
    # This ensures user_app is rebuilt when idf.py flash command is used.
    add_dependencies(flash user_app)
endif()
