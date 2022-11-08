idf_build_get_property(target IDF_TARGET)
idf_build_get_property(sdkconfig_header SDKCONFIG_HEADER)
idf_build_get_property(idf_path IDF_PATH)
if((${IDF_VERSION_MAJOR} EQUAL 4) AND (${IDF_VERSION_MINOR} EQUAL 3))
    if("${target}" STREQUAL "esp32s3")
        message(FATAL_ERROR "ESP Privilege Separation on ESP32-S3 is not supported on v4.3. Please use v4.4 and above")
    endif()

    return()

elseif((${IDF_VERSION_MAJOR} EQUAL 4) AND (${IDF_VERSION_MINOR} EQUAL 4))
    include(${idf_path}/components/esp32s3/CMakeLists.txt)
endif()
