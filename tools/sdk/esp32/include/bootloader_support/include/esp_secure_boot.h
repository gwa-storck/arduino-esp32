// Copyright 2015-2016 Espressif Systems (Shanghai) PTE LTD
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at

//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
#pragma once

#include <stdbool.h>
#include <esp_err.h>
#include "soc/efuse_periph.h"
#include "esp_image_format.h"
#include "esp_rom_efuse.h"
#include "sdkconfig.h"

#if CONFIG_IDF_TARGET_ESP32
#include "esp32/rom/secure_boot.h"
#endif

typedef struct ets_secure_boot_signature ets_secure_boot_signature_t;

#ifdef CONFIG_SECURE_BOOT_V1_ENABLED
#if !defined(CONFIG_SECURE_SIGNED_ON_BOOT) || !defined(CONFIG_SECURE_SIGNED_ON_UPDATE) || !defined(CONFIG_SECURE_SIGNED_APPS)
#error "internal sdkconfig error, secure boot should always enable all signature options"
#endif
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Support functions for secure boot features.

   Can be compiled as part of app or bootloader code.
*/

/** @brief Is secure boot currently enabled in hardware?
 *
 * This means that the ROM bootloader code will only boot
 * a verified secure bootloader from now on.
 *
 * @return true if secure boot is enabled.
 */
static inline bool esp_secure_boot_enabled(void)
{
#if CONFIG_IDF_TARGET_ESP32
    #ifdef CONFIG_SECURE_BOOT_V1_ENABLED
        return REG_READ(EFUSE_BLK0_RDATA6_REG) & EFUSE_RD_ABS_DONE_0;
    #elif CONFIG_SECURE_BOOT_V2_ENABLED
        return ets_use_secure_boot_v2();
    #endif
#else
    return esp_rom_efuse_is_secure_boot_enabled();
#endif
    return false; /* Secure Boot not enabled in menuconfig */
}

/** @brief Generate secure digest from bootloader image
 *
 * @important This function is intended to be called from bootloader code only.
 *
 * This function is only used in the context of the Secure Boot V1 scheme.
 * 
 * If secure boot is not yet enabled for bootloader, this will:
 *     1) generate the secure boot key and burn it on EFUSE
 *        (without enabling R/W protection)
 *     2) generate the digest from bootloader and save it
 *        to flash address 0x0
 *
 * If first boot gets interrupted after calling this function
 * but before esp_secure_boot_permanently_enable() is called, then
 * the key burned on EFUSE will not be regenerated, unless manually
 * done using espefuse.py tool
 *
 * @return ESP_OK if secure boot digest is generated
 * successfully or found to be already present
 */
esp_err_t esp_secure_boot_generate_digest(void);

/** @brief Enable secure boot V1 if it is not already enabled.
 *
 * @important If this function succeeds, secure boot V1 is permanently
 * enabled on the chip via efuse.
 *
 * @important This function is intended to be called from bootloader code only.
 *
 * @important In case of Secure Boot V1, this will enable r/w protection 
 * of secure boot key on EFUSE, therefore it is to be ensured that 
 * esp_secure_boot_generate_digest() is called before this .If secure boot is not 
 * yet enabled for bootloader, this will
 *     1) enable R/W protection of secure boot key on EFUSE
 *     2) enable secure boot by blowing the EFUSE_RD_ABS_DONE_0 efuse.
 *
 * This function does not verify secure boot of the bootloader (the
 * ROM bootloader does this.)
 *
 * Will fail if efuses have been part-burned in a way that indicates
 * secure boot should not or could not be correctly enabled.
 *
 * @return ESP_ERR_INVALID_STATE if efuse state doesn't allow
 * secure boot to be enabled cleanly. ESP_OK if secure boot
 * is enabled on this chip from now on.
 */
esp_err_t esp_secure_boot_permanently_enable(void);

/** @brief Enables secure boot V2 if it is not already enabled.
 *
 * @important If this function succeeds, secure boot V2 is permanently
 * enabled on the chip via efuse.
 *
 * @important This function is intended to be called from bootloader code only.
 * 
 * @important In case of Secure Boot V2, this will enable write protection 
 * of secure boot key on EFUSE in BLK2. .If secure boot is not 
 * yet enabled for bootloader, this will
 *     1) enable W protection of secure boot key on EFUSE
 *     2) enable secure boot by blowing the EFUSE_RD_ABS_DONE_1 efuse.
 *
 * This function does not verify secure boot of the bootloader (the
 * ROM bootloader does this.)
 *
 * @param image_data Image metadata of the application to be loaded.
 * 
 * Will fail if efuses have been part-burned in a way that indicates
 * secure boot should not or could not be correctly enabled.
 *
 * @return ESP_ERR_INVALID_STATE if efuse state doesn't allow
 * secure boot to be enabled cleanly. ESP_OK if secure boot
 * is enabled on this chip from now on.
 */
esp_err_t esp_secure_boot_v2_permanently_enable(const esp_image_metadata_t *image_data);

/** @brief Verify the secure boot signature appended to some binary data in flash.
 *
 * For ECDSA Scheme (Secure Boot V1) - deterministic ECDSA w/ SHA256 image
 * For RSA Scheme (Secure Boot V2) - RSA-PSS Verification of the SHA-256 image
 * 
 * Public key is compiled into the calling program in the ECDSA Scheme.
 * See the apt docs/security/secure-boot-v1.rst or docs/security/secure-boot-v2.rst for details.
 *
 * @param src_addr Starting offset of the data in flash.
 * @param length Length of data in bytes. Signature is appended -after- length bytes.
 *
 * If flash encryption is enabled, the image will be transparently decrypted while being verified.
 *
 * @note This function doesn't have any fault injection resistance so should not be called
 * during a secure boot itself (but can be called when verifying an update, etc.)
 *
 * @return ESP_OK if signature is valid, ESP_ERR_INVALID_STATE if
 * signature fails, ESP_FAIL for other failures (ie can't read flash).
 */
esp_err_t esp_secure_boot_verify_signature(uint32_t src_addr, uint32_t length);

/** @brief Secure boot verification block, on-flash data format. */
typedef struct {
    uint32_t version;
    uint8_t signature[64];
} esp_secure_boot_sig_block_t;

/** @brief Verify the ECDSA secure boot signature block for Secure Boot V1.
 *
 *  Calculates Deterministic ECDSA w/ SHA256 based on the SHA256 hash of the image. ECDSA signature
 *  verification must be enabled in project configuration to use this function.
 *
 * Similar to esp_secure_boot_verify_signature(), but can be used when the digest is precalculated.
 * @param sig_block Pointer to ECDSA signature block data
 * @param image_digest Pointer to 32 byte buffer holding SHA-256 hash.
 * @param verified_digest Pointer to 32 byte buffer that will receive verified digest if verification completes. (Used during bootloader implementation only, result is invalid otherwise.)
 *
 */
esp_err_t esp_secure_boot_verify_ecdsa_signature_block(const esp_secure_boot_sig_block_t *sig_block, const uint8_t *image_digest, uint8_t *verified_digest);


/** @brief Verify the RSA secure boot signature block for Secure Boot V2.
 *
 *  Performs RSA-PSS Verification of the SHA-256 image based on the public key
 *  in the signature block, compared against the public key digest stored in efuse.
 *
 * Similar to esp_secure_boot_verify_signature(), but can be used when the digest is precalculated.
 * @param sig_block Pointer to RSA signature block data
 * @param image_digest Pointer to 32 byte buffer holding SHA-256 hash.
 * @param verified_digest Pointer to 32 byte buffer that will receive verified digest if verification completes. (Used during bootloader implementation only, result is invalid otherwise.)
 *
 */
esp_err_t esp_secure_boot_verify_rsa_signature_block(const ets_secure_boot_signature_t *sig_block, const uint8_t *image_digest, uint8_t *verified_digest);

/** @brief Legacy ECDSA verification function
 *
 * @note Deprecated, call either esp_secure_boot_verify_ecdsa_signature_block() or esp_secure_boot_verify_rsa_signature_block() instead.
 *
 * @param sig_block Pointer to ECDSA signature block data
 * @param image_digest Pointer to 32 byte buffer holding SHA-256 hash.
 */
esp_err_t esp_secure_boot_verify_signature_block(const esp_secure_boot_sig_block_t *sig_block, const uint8_t *image_digest)
    __attribute__((deprecated("use esp_secure_boot_verify_ecdsa_signature_block instead")));


#define FLASH_OFFS_SECURE_BOOT_IV_DIGEST 0

/** @brief Secure boot IV+digest header */
typedef struct {
    uint8_t iv[128];
    uint8_t digest[64];
} esp_secure_boot_iv_digest_t;

#ifdef __cplusplus
}
#endif