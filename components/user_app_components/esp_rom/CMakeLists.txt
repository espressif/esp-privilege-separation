idf_build_get_property(target IDF_TARGET)
idf_build_get_property(idf_path IDF_PATH)

set(esp_rom_path ${idf_path}/components/esp_rom)

set (include_dirs "${esp_rom_path}/include"
                  "${esp_rom_path}/${target}"
                  "${esp_rom_path}/include/${target}")

idf_component_register(INCLUDE_DIRS "${include_dirs}")
