idf_build_get_property(target IDF_TARGET)

if(USER_APP_BUILD)
    set(srcs)
else()
    set(srcs "esp_priv_access_image_utility.c"
             "esp_priv_access_setup.c"
             "esp_priv_access_ota_utils.c"
             "esp_priv_access_panic.c")
    set(cacert)
endif()

if(CONFIG_PA_ENABLE_USER_APP_SECURE_BOOT)
    idf_build_get_property(project_dir PROJECT_DIR)
    get_filename_component(ROOT_DIR ${CMAKE_CURRENT_LIST_DIR}/../../.. ABSOLUTE)
    set(secure_boot_util "python ${ROOT_DIR}/tools/esp_ps_secure_boot_util.py")

    # The CA certificate is given relative to main project directory.
    # Get the absolute path to CA certificate
    get_filename_component(protected_ca_cert
            "${CONFIG_PA_CA_CERT_FILE}"
            ABSOLUTE BASE_DIR "${project_dir}")

    get_filename_component(protected_ca_dir
        "${protected_ca_cert}"
        DIRECTORY)

    if (NOT EXISTS "${protected_ca_cert}")
        message(FATAL_ERROR
            "Protected CA certificate not found."
            "\nTo generate a CA certificate:"
            "\n\t${secure_boot_util} generate_protected_ca "
            "${protected_ca_dir}/protected_priv_key.pem ${protected_ca_cert}"
            "\nIf already generated, please modify the CA certificate location from menuconfig")
    endif()
    list(APPEND srcs "esp_priv_access_secure_boot.c")
    list(APPEND cacert ${protected_ca_cert})
endif()

idf_component_get_property(bootloader_support_dir bootloader_support COMPONENT_DIR)

set(includes "include"
             "include/${target}"
             "${bootloader_support_dir}/include_bootloader")

idf_component_register(SRCS ${srcs}
                    INCLUDE_DIRS ${includes}
                    PRIV_INCLUDE_DIRS "."
                    PRIV_REQUIRES bootloader_support spi_flash app_update console esp_priv_build_utils mbedtls
                    EMBED_TXTFILES ${cacert})

if(CONFIG_PA_ENABLE_USER_APP_SECURE_BOOT OR CONFIG_SECURE_SIGNED_ON_UPDATE_NO_SECURE_BOOT)
    target_link_libraries(${COMPONENT_LIB} INTERFACE "-Wl,--wrap=esp_secure_boot_verify_rsa_signature_block")
endif()
