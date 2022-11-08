idf_build_get_property(idf_path IDF_PATH)
set(esp_system_dir ${idf_path}/components/esp_system)

set(priv_requires spi_flash esp_pm app_update nvs_flash pthread app_trace esp_gdbstub)
set(ld_fragments ${esp_system_dir}/linker.lf)

if((${IDF_VERSION_MAJOR} EQUAL 4) AND (${IDF_VERSION_MINOR} EQUAL 3))
    set(srcs "${esp_system_dir}/intr_alloc.c"
             "${esp_system_dir}/esp_async_memcpy.c"
             "${esp_system_dir}/panic.c"
             "${esp_system_dir}/system_api.c"
             "${esp_system_dir}/startup.c"
             "${esp_system_dir}/system_time.c"
             "${esp_system_dir}/sleep_modes.c")
elseif((${IDF_VERSION_MAJOR} EQUAL 4) AND (${IDF_VERSION_MINOR} EQUAL 4))
    set(srcs "${esp_system_dir}/esp_err.c")

    if(CONFIG_IDF_ENV_FPGA)
        list(APPEND srcs "${esp_system_dir}/fpga_overrides.c")
    endif()

    list(APPEND srcs "${esp_system_dir}/crosscore_int.c"
            "${esp_system_dir}/esp_err.c"
            "${esp_system_dir}/freertos_hooks.c"
            "${esp_system_dir}/int_wdt.c"
            "${esp_system_dir}/panic.c"
            "${esp_system_dir}/esp_system.c"
            "${esp_system_dir}/startup.c"
            "${esp_system_dir}/system_time.c"
            "${esp_system_dir}/stack_check.c"
            "${esp_system_dir}/task_wdt.c"
            "${esp_system_dir}/ubsan.c"
            "${esp_system_dir}/xt_wdt.c"
            "${esp_system_dir}/debug_stubs.c")

    if(CONFIG_ESP_SYSTEM_USE_EH_FRAME)
        list(APPEND srcs "${esp_system_dir}/eh_frame_parser.c")
    endif()
    list(APPEND priv_requires espcoredump esp_phy efuse esp_ipc)
    list(APPEND ld_fragments ${esp_system_dir}/app.lf)
endif()

idf_component_register(SRCS ${srcs}
                       INCLUDE_DIRS ${esp_system_dir}/include
                       PRIV_REQUIRES ${priv_requires}
                       LDFRAGMENTS ${ld_fragments})

#add_subdirectory(${esp_system_dir}/port ${CMAKE_CURRENT_BINARY_DIR}/port)
target_include_directories(${COMPONENT_LIB} PRIVATE ${esp_system_dir}/port/include)
target_include_directories(${COMPONENT_LIB} PUBLIC ${esp_system_dir}/port/public_compat)

target_include_directories(${COMPONENT_LIB} PUBLIC public_compat)
