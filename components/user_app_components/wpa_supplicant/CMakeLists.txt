idf_build_get_property(idf_path IDF_PATH)
set(wpa_supplicant_dir ${idf_path}/components/wpa_supplicant)

idf_component_register(INCLUDE_DIRS ${wpa_supplicant_dir}/include)
