set(srcs "syscall_wrappers.c")

set(includes "include")

idf_component_register(SRCS ${srcs}
                    INCLUDE_DIRS ${includes}
                    PRIV_INCLUDE_DIRS "."
                    PRIV_REQUIRES esp_wifi syscall_shared esp_priv_build_utils)
