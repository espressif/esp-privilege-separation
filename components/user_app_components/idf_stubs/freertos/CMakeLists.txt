idf_build_get_property(target IDF_TARGET)
idf_build_get_property(idf_path IDF_PATH)
set(freertos_dir ${idf_path}/components/freertos)

if(NOT "${target}" STREQUAL "esp32c3")  # should test arch here not target, TODO ESP32-C3 IDF-1754
    set(include_dirs
        include
        port/xtensa/include)

    set(required_components app_trace esp_timer)

else()  # RISC-V
    set(include_dirs
        ${freertos_dir}/include
        ${freertos_dir}/port/riscv/include)

    if((${IDF_VERSION_MAJOR} EQUAL 4) AND (${IDF_VERSION_MINOR} EQUAL 4))
        list(APPEND include_dirs
            ${freertos_dir}/include/esp_additions/freertos
            ${freertos_dir}/include/esp_additions)
    endif()

    set(required_components esp_timer)

endif()

# app_trace is required by FreeRTOS headers only when CONFIG_SYSVIEW_ENABLE=y,
# but requirements can't depend on config options, so always require it.
idf_component_register(SRCS freertos_stubs.c
                       INCLUDE_DIRS ${include_dirs}
                       REQUIRES ${required_components})

idf_component_set_property(freertos ORIG_INCLUDE_PATH "${freertos_dir}/include/freertos/")
