set(srcs "startup_utilities.c")

if(CONFIG_IDF_TARGET_ARCH_XTENSA)
    list(APPEND srcs "xtensa/user_app_vectors.S")
endif()

idf_component_register(SRCS ${srcs}
                    PRIV_INCLUDE_DIRS "."
                    PRIV_REQUIRES syscall_shared esp_priv_build_utils user_console)

if(CONFIG_IDF_TARGET_ARCH_XTENSA)
    target_link_libraries(${COMPONENT_TARGET} "-u force_linker_to_include")
endif()
