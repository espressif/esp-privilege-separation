set(srcs "startup_utilities.c")

idf_component_register(SRCS ${srcs}
                    PRIV_INCLUDE_DIRS "."
                    PRIV_REQUIRES syscall_shared esp_priv_build_utils)
