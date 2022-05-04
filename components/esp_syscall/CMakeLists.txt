if(USER_APP_BUILD)
    set(srcs)
else()
    set(srcs
        "src/esp_syscalls.c"
        "src/syscall_handler.S"
        "src/esp_map.c"
        "src/esp_syscall_table.c")
    set(priv_include "priv_include")
endif()

set(includes
    "include"
    "${CMAKE_CURRENT_BINARY_DIR}"
    )


idf_component_register(SRCS ${srcs}
                    INCLUDE_DIRS ${includes}
                    PRIV_INCLUDE_DIRS ${priv_include}
                    REQUIRES esp_wifi nvs_flash esp-tls esp_priv_access build_utility)

set(syscallhdr_sh
    ${COMPONENT_DIR}/syscall/syscallhdr.sh ${CMAKE_CURRENT_BINARY_DIR}/syscall.tbl
    )

set(syscalltbl_sh
    ${COMPONENT_DIR}/syscall/syscalltbl.sh ${CMAKE_CURRENT_BINARY_DIR}/syscall.tbl
    )

set(syscall_def_h
    ${CONFIG_DIR}/syscall_def.h
    )

set(esp_syscall_h
    ${CMAKE_CURRENT_BINARY_DIR}/esp_syscall.h
    )

set(syscall_dec_h
    ${CMAKE_CURRENT_BINARY_DIR}/syscall_dec.h)

add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/syscall.tbl
    PRE_BUILD
    COMMAND cat ${COMPONENT_DIR}/syscall/syscall.tbl ${CUSTOM_SYSCALL_TBL} > syscall.tbl
    DEPENDS ${COMPONENT_DIR}/syscall/syscall.tbl ${CUSTOM_SYSCALL_TBL}
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    VERBATIM
    COMMENT "Generating syscall.tbl")
add_custom_target(syscall_tbl DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/syscall.tbl)

add_custom_command(OUTPUT ${syscall_def_h}
    PRE_BUILD
    COMMAND ${syscallhdr_sh} ${syscall_def_h} ${syscall_dec_h}
    DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/syscall.tbl
    VERBATIM
    COMMENT "Generating syscall_def.h")

add_custom_command(OUTPUT ${esp_syscall_h}
    PRE_BUILD
    COMMAND ${syscalltbl_sh} ${esp_syscall_h}
    DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/syscall.tbl
    VERBATIM
    COMMENT "Generating esp_syscall.h")

add_custom_target(syscall_headers DEPENDS ${syscall_def_h} ${esp_syscall_h})
add_dependencies(${COMPONENT_LIB} syscall_headers syscall_tbl)

set_property(DIRECTORY "${COMPONENT_DIR}" APPEND PROPERTY
     ADDITIONAL_MAKE_CLEAN_FILES ${syscall_def_h} ${esp_syscall_h})
