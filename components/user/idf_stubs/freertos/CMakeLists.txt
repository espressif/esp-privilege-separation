idf_build_get_property(target IDF_TARGET)
idf_build_get_property(idf_path IDF_PATH)
set(freertos_dir ${idf_path}/components/freertos)

if(CONFIG_IDF_TARGET_ARCH_XTENSA)
    set(include_dirs
        ${freertos_dir}/include
        ${freertos_dir}/port/xtensa/include)

    if((${IDF_VERSION_MAJOR} EQUAL 4) AND (${IDF_VERSION_MINOR} EQUAL 4))
        list(APPEND include_dirs
            ${freertos_dir}/include/esp_additions/freertos
            ${freertos_dir}/include/esp_additions)
    endif()

elseif(CONFIG_IDF_TARGET_ARCH_RISCV)
    set(include_dirs
        ${freertos_dir}/include
        ${freertos_dir}/port/riscv/include)

    if((${IDF_VERSION_MAJOR} EQUAL 4) AND (${IDF_VERSION_MINOR} EQUAL 4))
        list(APPEND include_dirs
            ${freertos_dir}/include/esp_additions/freertos
            ${freertos_dir}/include/esp_additions)
    endif()

endif()

# app_trace is required by FreeRTOS headers only when CONFIG_SYSVIEW_ENABLE=y,
# but requirements can't depend on config options, so always require it.
set(required_components app_trace esp_timer)

idf_component_register(SRCS freertos_stubs.c
                       INCLUDE_DIRS ${include_dirs}
                       REQUIRES ${required_components})

idf_component_set_property(freertos ORIG_INCLUDE_PATH "${freertos_dir}/include/freertos/")
