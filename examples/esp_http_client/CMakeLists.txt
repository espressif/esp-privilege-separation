# (Automatically converted from project Makefile by convert_to_cmake.py.)

# The following lines of boilerplate have to be in your project's CMakeLists
# in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.5)

set(EXTRA_COMPONENT_DIRS ${CMAKE_CURRENT_LIST_DIR}/../../components/protected
                         ${CMAKE_CURRENT_LIST_DIR}/../../components/shared
                         ${CMAKE_CURRENT_LIST_DIR}/protected_app)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(esp_http_client)
