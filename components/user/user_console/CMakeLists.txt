idf_component_register(SRCS "user_commands.c"
                            "user_console.c"
                       INCLUDE_DIRS .
                       REQUIRES console syscall_shared)
