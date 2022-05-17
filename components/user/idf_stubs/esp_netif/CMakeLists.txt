idf_build_get_property(idf_path IDF_PATH)
set(esp_netif_dir ${idf_path}/components/esp_netif)

idf_component_register(INCLUDE_DIRS ${esp_netif_dir}/include
                       REQUIRES lwip esp_eth tcpip_adapter)
