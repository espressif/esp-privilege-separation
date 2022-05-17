idf_build_get_property(idf_path IDF_PATH)
idf_build_get_property(idf_target IDF_TARGET)
set(esp_wifi_dir ${idf_path}/components/esp_wifi)

if((${IDF_VERSION_MAJOR} EQUAL 4) AND (${IDF_VERSION_MINOR} EQUAL 3))
    set(include_dirs ${esp_wifi_dir}/include ${esp_wifi_dir}/${idf_target}/include)
elseif((${IDF_VERSION_MAJOR} EQUAL 4) AND (${IDF_VERSION_MINOR} EQUAL 4))
    set(include_dirs ${esp_wifi_dir}/include)
endif()
idf_component_register(INCLUDE_DIRS ${include_dirs}
                       REQUIRES esp_event)
