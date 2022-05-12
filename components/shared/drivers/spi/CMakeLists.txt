if(USER_APP_BUILD)
    set(src)
else()
    set(src "vfs_spi.c")
endif()

idf_component_register(SRCS ${src}
                       INCLUDE_DIRS "include")
