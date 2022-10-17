idf_component_register(INCLUDE_DIRS "include")

if (USER_APP_BUILD)

    idf_build_set_property(COMPILE_OPTIONS "-fno-builtin" APPEND)
    idf_build_set_property(COMPILE_OPTIONS "-Wno-format" APPEND)

    if((${IDF_VERSION_MAJOR} EQUAL 4) AND (${IDF_VERSION_MINOR} EQUAL 3))
        set(peripherals_ld $ENV{IDF_PATH}/components/${IDF_TARGET}/ld/${IDF_TARGET}.peripherals.ld)
    elseif((${IDF_VERSION_MAJOR} EQUAL 4) AND (${IDF_VERSION_MINOR} EQUAL 4))
        set(peripherals_ld $ENV{IDF_PATH}/components/soc/${IDF_TARGET}/ld/${IDF_TARGET}.peripherals.ld)
    endif()
    target_linker_script(${COMPONENT_LIB} INTERFACE ${peripherals_ld})

    target_linker_script(${COMPONENT_LIB} INTERFACE "${CMAKE_CURRENT_BINARY_DIR}/user_out.ld")

    set(LD_DIR ${COMPONENT_DIR}/ld/user_app/${IDF_TARGET})

    add_custom_command(
        OUTPUT user_out.ld
        COMMAND "${CMAKE_C_COMPILER}" -C -P -x c -E -o user_out.ld -I ${PROTECTED_CONFIG_DIR} -I ${PROJECT_DIR} ${LD_DIR}/user.ld
        MAIN_DEPENDENCY ${LD_DIR}/user.ld ${PROJECT_DIR}/user_config.h
        DEPENDS ${PROTECTED_CONFIG_DIR}/memory_layout.h
        COMMENT "Generating linker script..."
        VERBATIM)

    add_custom_target(user_linker_script DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/user_out.ld)
    add_dependencies(${COMPONENT_LIB} user_linker_script)

    target_link_libraries(${COMPONENT_LIB} INTERFACE "-u _user_main")
    target_link_libraries(${COMPONENT_LIB} INTERFACE "-u user_main")
    target_link_libraries(${COMPONENT_LIB} INTERFACE "-u user_app_desc")
    target_link_libraries(${COMPONENT_LIB} INTERFACE "-u custom_app_desc")
    target_link_libraries(${COMPONENT_LIB} INTERFACE "${DEFSYM_LIST}")

else()

    # errno for user tasks is stored in TLS. --wrap __errno to use TLS errno if it is a user task.
    target_link_libraries(${COMPONENT_LIB} INTERFACE "-Wl,--wrap=__errno")

    # If user app secure boot is enabled, do not add the binary in the flash command
    # User is required to sign and flash the binary separately
    if(NOT CONFIG_PA_ENABLE_USER_APP_SECURE_BOOT)
        # Add user_app binary to the esptool flash arguments
        partition_table_get_partition_info(partition "--partition-type app --partition-subtype user_0" "name")
        set(image_file ${PROJECT_DIR}/build/user_app/user_app.bin)
        partition_table_get_partition_info(offset "--partition-name ${partition}" "offset")
        esptool_py_flash_target_image(flash "${partition}" "${offset}" "${image_file}")
    endif()

    # Get info about user_otadata partition
    partition_table_get_partition_info(otadata_offset "--partition-type data --partition-subtype user_ota" "offset")
    partition_table_get_partition_info(otadata_size "--partition-type data --partition-subtype user_ota" "size")

    if(otadata_size AND otadata_offset)
        idf_build_get_property(build_dir BUILD_DIR)
        set(blank_user_otadata_file ${build_dir}/user_ota_data_initial.bin)
        idf_build_get_property(idf_path IDF_PATH)
        idf_build_get_property(python PYTHON)

        idf_component_get_property(partition_table_dir partition_table COMPONENT_DIR)

        add_custom_command(OUTPUT ${blank_user_otadata_file}
            COMMAND ${python} ${partition_table_dir}/gen_empty_partition.py
            ${otadata_size} ${blank_user_otadata_file})

        add_custom_target(blank_user_ota_data ALL DEPENDS ${blank_user_otadata_file})
        add_dependencies(flash blank_user_ota_data)
        partition_table_get_partition_info(otadata_part_name "--partition-type data --partition-subtype user_ota" "name")
        esptool_py_flash_target_image(flash "${otadata_part_name}" "${otadata_offset}" "${blank_user_otadata_file}")
    endif()

    # Add dependency of idf.py flash on user_app.
    # This ensures user_app is rebuilt when idf.py flash command is used.
    add_dependencies(flash user_app)
endif()
