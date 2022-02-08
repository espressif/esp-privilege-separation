idf_build_get_property(idf_path IDF_PATH)
set(spi_flash_dir ${idf_path}/components/spi_flash)

idf_component_register(INCLUDE_DIRS ${spi_flash_dir}/include
                       REQUIRES hal)
