idf_build_get_property(idf_path IDF_PATH)
set(vfs_dir ${idf_path}/components/vfs)

idf_component_register(INCLUDE_DIRS ${vfs_dir}/include)
