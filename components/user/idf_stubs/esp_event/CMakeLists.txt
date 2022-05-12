idf_build_get_property(idf_path IDF_PATH)
set(esp_event_dir ${idf_path}/components/esp_event)

idf_component_register(SRCS esp_event_definitions.c
                       INCLUDE_DIRS ${esp_event_dir}/include
                       REQUIRES esp_netif)
