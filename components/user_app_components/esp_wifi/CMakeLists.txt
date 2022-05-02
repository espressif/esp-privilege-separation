idf_build_get_property(idf_path IDF_PATH)
idf_build_get_property(idf_target IDF_TARGET)
set(esp_wifi_dir ${idf_path}/components/esp_wifi)

idf_component_register(INCLUDE_DIRS ${esp_wifi_dir}/include ${esp_wifi_dir}/${idf_target}/include
                       REQUIRES esp_event)
