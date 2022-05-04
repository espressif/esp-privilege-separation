set(srcs "syscall_wrappers.c")

set(includes "include")

idf_component_register(SRCS ${srcs}
                    INCLUDE_DIRS ${includes}
                    PRIV_INCLUDE_DIRS "."
                    REQUIRES esp_wifi nvs_flash esp-tls esp_priv_access esp_syscall)
