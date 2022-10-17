// Copyright 2020-2022 Espressif Systems (Shanghai) PTE LTD
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//         http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <string.h>
#include <freertos/FreeRTOS.h>
#include <esp_priv_access_ota_utils.h>
#include <esp_image_format.h>
#include <esp_log.h>

#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/x509_crt.h"
#include "mbedtls/error.h"
#include "mbedtls/platform.h"

#include "bootloader_utility.h"
#include "esp_secure_boot.h"
#include "esp_fault.h"

#define ALIGN_UP(num, align) (((num) + ((align) - 1)) & ~((align) - 1))
#define FLASH_SECTOR_SIZE       4096

#define USER_APP_SECURE_BOOT_MAGIC      0xB7
#define CRC_LEN                         4092            // sizeof(sig_block_t) - 4

static const char *TAG =  "user_secure_boot";

extern const uint8_t protected_ca_cert_pem_start[] asm("_binary_protected_ca_cert_pem_start");
extern const uint8_t protected_ca_cert_pem_end[]   asm("_binary_protected_ca_cert_pem_end");

/* The signature block consists of a signature section and the certificate.
 * Certificate is placed right after the signature section
 */
typedef struct {
    uint8_t magic_byte;
    uint8_t version;
    uint8_t _reserved1;
    uint8_t _reserved2;
    uint8_t image_digest[32];
    uint8_t signature[384];
    uint32_t cert_len;
    unsigned char user_cert[3664];
    uint8_t _padding[4];
    uint32_t block_crc;
} sig_block_t;

_Static_assert(sizeof(sig_block_t) == 4096, "Invalid signature block size");

/* A signature block is valid when it has correct magic byte, crc. */
static esp_err_t validate_signature_block(const sig_block_t *block)
{
    if (block->magic_byte != USER_APP_SECURE_BOOT_MAGIC
        || block->block_crc != esp_rom_crc32_le(0, (uint8_t *)block, CRC_LEN)) {
        return ESP_FAIL;
    }
    return ESP_OK;
}

static esp_err_t esp_priv_access_verify_rsa_signature_block(const sig_block_t *sig_block, const uint8_t *image_digest, mbedtls_rsa_context *rsa_pubkey)
{
    int ret = 0;
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context ctr_drbg;

    mbedtls_entropy_init(&entropy);
    mbedtls_ctr_drbg_init(&ctr_drbg);
    ret = mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy, NULL, 0);
    if (ret != 0) {
        ESP_LOGE(TAG, "mbedtls_ctr_drbg_seed returned -0x%04x", -ret);
        goto exit;
    }

    if (validate_signature_block(sig_block) != ESP_OK) {
        ESP_LOGE(TAG, "Invalid signature block");
        return ESP_ERR_IMAGE_INVALID;
    }

    ESP_LOGI(TAG, "Verifying with RSA-PSS...");

    ret = mbedtls_rsa_rsassa_pss_verify(rsa_pubkey, mbedtls_ctr_drbg_random, &ctr_drbg, MBEDTLS_RSA_PUBLIC, MBEDTLS_MD_SHA256, ESP_SECURE_BOOT_DIGEST_LEN, image_digest, sig_block->signature);
    if (ret != 0) {
        ESP_LOGE(TAG, "Failed mbedtls_rsa_rsassa_pss_verify, err: -0x%04x", -ret);
    } else {
        ESP_FAULT_ASSERT(ret == 0);
        ESP_LOGI(TAG, "Signature verified successfully!");
    }

exit:
    return (ret != 0) ? ESP_ERR_IMAGE_INVALID: ESP_OK;
}

static esp_err_t verify_user_app_certificate(const sig_block_t *sig_block, mbedtls_rsa_context *rsa_pubkey)
{
    int ret;
    uint32_t flags = 0;
    esp_err_t err = ESP_OK;
    mbedtls_x509_crt *cacert;
    mbedtls_x509_crt *user_cert;

    cacert = malloc(sizeof(mbedtls_x509_crt));
    if (cacert == NULL) {
        ESP_LOGE(TAG, "Error allocating memory for CA cert");
        return ESP_FAIL;
    }

    user_cert = malloc(sizeof(mbedtls_x509_crt));
    if (user_cert == NULL) {
        free(cacert);
        ESP_LOGE(TAG, "Error allocating memory for user cert");
        return ESP_FAIL;
    }

    mbedtls_x509_crt_init(cacert);
    mbedtls_x509_crt_init(user_cert);

    /* Parse trusted CA cert in protected app */
    ret = mbedtls_x509_crt_parse(cacert, protected_ca_cert_pem_start,
                                 protected_ca_cert_pem_end - protected_ca_cert_pem_start);
    if (ret != 0) {
        ESP_LOGE(TAG, "Error parsing protected CA certificate (err: -0x%04x)", -ret);
        err = ESP_ERR_IMAGE_INVALID;
        goto exit;
    }

    /* Parse user certificate retrieved from the flash */
    ret = mbedtls_x509_crt_parse(user_cert, sig_block->user_cert, sig_block->cert_len);
    if (ret != 0) {
        ESP_LOGE(TAG, "Error parsing user app certificate (err: -0x%04x)", -ret);
        err = ESP_ERR_IMAGE_INVALID;
        goto exit;
    }

    /* Verify if the user certificate is signed by the trusted CA using the CA cert */
    ret = mbedtls_x509_crt_verify(user_cert, cacert, NULL, NULL, &flags, NULL, NULL);
    if (ret != 0) {
        ESP_LOGE(TAG, "Invalid user certificate (err: -0x%04x)", -ret);
        err = ESP_ERR_IMAGE_INVALID;
        goto exit;
    }

    /* Extract the public key from the verified user app certificate */
    const mbedtls_rsa_context *rsa_context = mbedtls_pk_rsa(user_cert->pk);
    mbedtls_rsa_import(rsa_pubkey, &rsa_context->N, NULL, NULL, NULL, &rsa_context->E);
    ret = mbedtls_rsa_complete(rsa_pubkey);
    if (ret != 0) {
        ESP_LOGE(TAG, "Error importing public key (err: -0x%04x)", -ret);
    }

exit:
    mbedtls_x509_crt_free(user_cert);
    mbedtls_x509_crt_free(cacert);
    free(user_cert);
    free(cacert);

    return err;
}

esp_err_t esp_priv_access_verify_user_app(const esp_partition_t *user_partition)
{
    esp_err_t err;
    const void *sig_block = NULL;
    spi_flash_mmap_handle_t block_map;
    esp_image_metadata_t metadata;
    uint8_t digest[ESP_SECURE_BOOT_DIGEST_LEN] = {0};

    const esp_partition_pos_t part_pos = {
        .offset = user_partition->address,
        .size   = user_partition->size,
    };

    err = esp_image_get_metadata(&part_pos, &metadata);

    bootloader_sha256_flash_contents(metadata.start_addr, ALIGN_UP(metadata.image_len, FLASH_SECTOR_SIZE), digest);

    size_t sig_block_addr = ALIGN_UP(metadata.image_len, FLASH_SECTOR_SIZE);

    err = esp_partition_mmap(user_partition, sig_block_addr, FLASH_SECTOR_SIZE, SPI_FLASH_MMAP_DATA, &sig_block, &block_map);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "User signature block mmap failed (err: 0x%x)", err);
        return ESP_ERR_IMAGE_INVALID;
    }

    mbedtls_rsa_context *rsa_pubkey = malloc(sizeof(mbedtls_rsa_context));
    if (rsa_pubkey == NULL) {
        ESP_LOGE(TAG, "Error allocating memory for public key");
        return ESP_ERR_NO_MEM;
    }
    mbedtls_rsa_init(rsa_pubkey, MBEDTLS_RSA_PKCS_V21, MBEDTLS_MD_SHA256);
    err = verify_user_app_certificate((const sig_block_t *)sig_block, rsa_pubkey);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "User app certificate verification failed");
        goto exit;
    }

    err = esp_priv_access_verify_rsa_signature_block((const sig_block_t *)sig_block, digest, rsa_pubkey);

exit:
    mbedtls_rsa_free(rsa_pubkey);
    free(rsa_pubkey);
    spi_flash_munmap(block_map);

    return err;
}

extern esp_err_t __real_esp_secure_boot_verify_rsa_signature_block(const ets_secure_boot_signature_t *sig_block, const uint8_t *image_digest, uint8_t *verified_digest);

esp_err_t __wrap_esp_secure_boot_verify_rsa_signature_block(const ets_secure_boot_signature_t *sig_block, const uint8_t *image_digest, uint8_t *verified_digest)
{
    if (sig_block->block[0].magic_byte == USER_APP_SECURE_BOOT_MAGIC) {
        esp_err_t err;
        mbedtls_rsa_context *rsa_pubkey = malloc(sizeof(mbedtls_rsa_context));
        if (rsa_pubkey == NULL) {
            ESP_LOGE(TAG, "Error allocating memory for public key");
            return ESP_ERR_NO_MEM;
        }
        mbedtls_rsa_init(rsa_pubkey, MBEDTLS_RSA_PKCS_V21, MBEDTLS_MD_SHA256);
        sig_block_t *user_sig_block = (sig_block_t *)sig_block;
        err = verify_user_app_certificate(user_sig_block, rsa_pubkey);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "User app certificate verification failed");
            return err;
        }
        err = esp_priv_access_verify_rsa_signature_block(user_sig_block, image_digest, rsa_pubkey);
        mbedtls_rsa_free(rsa_pubkey);
        free(rsa_pubkey);
        return err;
    } else if (sig_block->block[0].magic_byte == ETS_SECURE_BOOT_V2_SIGNATURE_MAGIC) {
        return __real_esp_secure_boot_verify_rsa_signature_block(sig_block, image_digest, verified_digest);
    } else {
        return ESP_ERR_IMAGE_INVALID;
    }
}
