if(USER_APP_BUILD)
    set(src)
else()
    set(src "ws2812.c"
        "led_strip_rmt_ws2812.c")
endif()

idf_component_register(SRCS ${src}
                       INCLUDE_DIRS "include")
