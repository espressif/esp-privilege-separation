# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.5)

if(NOT IDF_PATH)
    message(FATAL_ERROR "IDF_PATH missing from parent build process")
endif()

if(NOT IDF_TARGET)
    message(FATAL_ERROR "IDF_TARGET missing from parent build process")
endif()

if (NOT SDKCONFIG)
    message(FATAL_ERROR "SDKCONFIG missing from parent build process")
endif()

if(NOT CONFIG_DIR)
    message(FATAL_ERROR "CONFIG_DIR missing from parent build process")
endif()

if(NOT SYSCALL_LIB)
    message(FATAL_ERROR "Path to prebuilt libesp_syscall.a missing from parent build process")
endif()

# Trim down the components since user app depends on these two
set(EXTRA_COMPONENT_DIRS ${CMAKE_CURRENT_LIST_DIR}/../../../components/user_app_components)
set(COMPONENTS esptool_py main)
set(BOOTLOADER_BUILD 1)

include(${IDF_PATH}/tools/cmake/project.cmake)

# Include sdkconfig.h derived from the protected_app build.
include_directories(${CONFIG_DIR})

# This is set in order to exclude the common components added by toplevel CMake
idf_build_set_property(__COMPONENT_REQUIRES_COMMON "")

# This is set in order to prevent sdkconfig to be overwritten
# sdkconfig is derived from the protected_app
idf_build_set_property(__OUTPUT_SDKCONFIG 0)

project(user_app)
