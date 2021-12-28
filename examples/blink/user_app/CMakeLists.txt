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

if(NOT PROTECTED_CONFIG_DIR)
    message(FATAL_ERROR "CONFIG_DIR missing from parent build process")
endif()

if(NOT SYSCALL_LIB)
    message(FATAL_ERROR "Path to prebuilt libesp_syscall.a missing from parent build process")
endif()

if (NOT DEFSYM_LIST)
    message(FATAL_ERROR "DEFSYM_LIST missing from parent build process")
endif()

set(USER_APP_BUILD 1)

set(ROOT_DIR ${CMAKE_CURRENT_LIST_DIR}/../../..)
set(EXTRA_COMPONENT_DIRS ${ROOT_DIR}/components ${ROOT_DIR}/components/user_app_components)

include(${IDF_PATH}/tools/cmake/project.cmake)

# Include sdkconfig.h derived from the protected_app build.
include_directories(${PROTECTED_CONFIG_DIR})

# This is set in order to prevent sdkconfig to be overwritten
# sdkconfig is derived from the protected_app
idf_build_set_property(__OUTPUT_SDKCONFIG 0)

# Compiler might optimize printf calls to puts which will generate linker errors
# Disable printf optimization
idf_build_set_property(COMPILE_OPTIONS "-nostdlib" APPEND)
idf_build_set_property(COMPILE_OPTIONS "-fno-builtin" APPEND)
idf_build_set_property(COMPILE_OPTIONS "-fno-inline" APPEND)
idf_build_set_property(COMPILE_OPTIONS "-Wno-format" APPEND)

project(user_app)
