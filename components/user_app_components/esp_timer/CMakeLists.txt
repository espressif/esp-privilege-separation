idf_build_get_property(idf_path IDF_PATH)
set(esp_timer_dir ${idf_path}/components/esp_timer)

idf_component_register(INCLUDE_DIRS ${esp_timer_dir}/include)
