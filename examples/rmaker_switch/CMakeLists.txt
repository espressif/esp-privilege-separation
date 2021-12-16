# (Automatically converted from project Makefile by convert_to_cmake.py.)

# The following lines of boilerplate have to be in your project's CMakeLists
# in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.5)

if (DEFINED ENV{RMAKER_PATH})
    set(RMAKER_PATH $ENV{RMAKER_PATH})
else()
    message(FATAL_ERROR "Rainmaker path not found. Please set RMAKER_PATH environment variable")
endif()

set(EXTRA_COMPONENT_DIRS ${CMAKE_CURRENT_LIST_DIR}/../../components/protected
                         ${CMAKE_CURRENT_LIST_DIR}/../../components/shared
                         ${CMAKE_CURRENT_LIST_DIR}/protected_app
                         ${RMAKER_PATH}/components
                         ${RMAKER_PATH}/components/esp-insights/components
                         ${RMAKER_PATH}/examples/common)

set(CUSTOM_SYSCALL_TBL ${CMAKE_CURRENT_LIST_DIR}/components/rmaker_syscall/rmaker_syscall.tbl)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

idf_build_set_property(EXTRA_CMAKE_ARGS "-DRMAKER_PATH=${RMAKER_PATH}" APPEND)

project(rmaker_switch)
