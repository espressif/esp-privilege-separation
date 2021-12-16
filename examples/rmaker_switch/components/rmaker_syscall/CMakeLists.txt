if(USER_APP_BUILD)
    set(srcs rmaker_wrappers.c)
else()
    set(srcs rmaker_syscalls.c)
endif()

idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS include
    PRIV_INCLUDE_DIRS .
    REQUIRES syscall_shared esp_priv_build_utils button ws2812_led esp_rainmaker rmaker_common app_wifi app_reset
    )

if(NOT USER_APP_BUILD)
    idf_component_get_property(esp_syscall_lib esp_syscall COMPONENT_LIB)
    target_link_libraries(${COMPONENT_LIB} PUBLIC ${esp_syscall_lib})
else()
    idf_component_get_property(syscall_wrapper_lib syscall_wrapper COMPONENT_LIB)
    target_link_libraries(${COMPONENT_LIB} PUBLIC ${syscall_wrapper_lib})
endif()
