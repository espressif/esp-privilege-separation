set(srcs
    "src/syscall_wrappers.c"
    "src/esp_syscalls.c"
    "src/syscall_handler.S")

idf_component_register(SRCS ${srcs}
                    INCLUDE_DIRS "include"
                    REQUIRES esp_wifi nvs_flash esp-tls esp_ps)

set(syscallhdr_sh
    ${COMPONENT_DIR}/syscall/syscallhdr.sh ${COMPONENT_DIR}/syscall/syscall.tbl
    )

set(syscalltbl_sh
    ${COMPONENT_DIR}/syscall/syscalltbl.sh ${COMPONENT_DIR}/syscall/syscall.tbl
    )

set(syscall_def_h
    ${COMPONENT_DIR}/include/syscall_def.h
    )

set(esp_syscall_h
    ${COMPONENT_DIR}/src/esp_syscall.h
    )

add_custom_command(OUTPUT ${syscall_def_h}
    PRE_BUILD
    COMMAND ${syscallhdr_sh} ${syscall_def_h}
    DEPENDS ${COMPONENT_DIR}/syscall/syscall.tbl
    VERBATIM
    COMMENT "Generating syscall_def.h")

add_custom_command(OUTPUT ${esp_syscall_h}
    PRE_BUILD
    COMMAND ${syscalltbl_sh} ${esp_syscall_h}
    DEPENDS ${COMPONENT_DIR}/syscall/syscall.tbl
    VERBATIM
    COMMENT "Generating esp_syscall.h")

add_custom_target(syscall_headers DEPENDS ${syscall_def_h} ${esp_syscall_h})
add_dependencies(${COMPONENT_LIB} syscall_headers)

set_property(DIRECTORY "${COMPONENT_DIR}" APPEND PROPERTY
     ADDITIONAL_MAKE_CLEAN_FILES ${syscall_def_h} ${esp_syscall_h})
