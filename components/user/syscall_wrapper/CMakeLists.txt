set(srcs "syscall_wrappers.c")

set(includes "include")

if(CONFIG_IDF_TARGET_ARCH_RISCV)
    list(APPEND includes "include/riscv")
elseif(CONFIG_IDF_TARGET_ARCH_XTENSA)
    list(APPEND includes "include/xtensa")
endif()


idf_component_register(SRCS ${srcs}
                    INCLUDE_DIRS ${includes}
                    PRIV_INCLUDE_DIRS "."
                    PRIV_REQUIRES esp_wifi syscall_shared esp_priv_build_utils)
