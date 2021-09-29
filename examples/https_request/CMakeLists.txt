# (Automatically converted from project Makefile by convert_to_cmake.py.)

# The following lines of boilerplate have to be in your project's CMakeLists
# in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.5)

set(EXTRA_COMPONENT_DIRS ${CMAKE_CURRENT_LIST_DIR}/../../components
                         ${CMAKE_CURRENT_LIST_DIR}/protected_app)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(https_request)

# Add user_app binary to the esptool flash arguments
set(partition user_app)
set(image_file ${PROJECT_DIR}/build/user_app/user_app.bin)
partition_table_get_partition_info(offset "--partition-name ${partition}" "offset")
esptool_py_flash_target_image(flash "${partition}" "${offset}" "${image_file}")
