idf_build_get_property(target IDF_TARGET)
idf_build_get_property(sdkconfig_header SDKCONFIG_HEADER)
idf_build_get_property(idf_path IDF_PATH)
if((${IDF_VERSION_MAJOR} EQUAL 4) AND (${IDF_VERSION_MINOR} EQUAL 3))
if(NOT "${target}" STREQUAL "esp32c3")
    return()
endif()

set(esp32c3_dir ${idf_path}/components/esp32c3)
# Regular app build

set(srcs "${esp32c3_dir}/cache_err_int.c"
         "${esp32c3_dir}/clk.c"
         "${esp32c3_dir}/crosscore_int.c"
         "${esp32c3_dir}/dport_access.c"
         "${esp32c3_dir}/esp_hmac.c"
         "${esp32c3_dir}/esp_ds.c"
         "${esp32c3_dir}/esp_crypto_lock.c"
         "${esp32c3_dir}/hw_random.c"
         "${esp32c3_dir}/memprot.c"
         "${esp32c3_dir}/system_api_esp32c3.c")
set(include_dirs "${esp32c3_dir}/include")

set(requires driver efuse soc riscv) #unfortunately rom/uart uses SOC registers directly

# driver is a public requirement because esp_sleep.h uses gpio_num_t & touch_pad_t
# app_update is added here because cpu_start.c uses esp_ota_get_app_description() function.
# esp_timer is added here because cpu_start.c uses esp_timer
set(priv_requires
    app_trace app_update bootloader_support log mbedtls nvs_flash
    pthread spi_flash vfs espcoredump esp_common esp_timer)

set(fragments linker.lf ld/esp32c3_fragments.lf)

idf_component_register(SRCS "${srcs}"
                    INCLUDE_DIRS "${include_dirs}"
                    LDFRAGMENTS "${fragments}"
                    REQUIRES "${requires}"
                    PRIV_REQUIRES "${priv_requires}"
                    REQUIRED_IDF_TARGETS esp32c3)

# disable stack protection in files which are involved in initialization of that feature
set_source_files_properties(
    cpu_start.c
    PROPERTIES COMPILE_FLAGS
    -fno-stack-protector)

elseif((${IDF_VERSION_MAJOR} EQUAL 4) AND (${IDF_VERSION_MINOR} EQUAL 4))
    if(NOT "${target}" STREQUAL "esp32c3")
        return()
    endif()
    include(${idf_path}/components/esp32c3/CMakeLists.txt)
endif()
