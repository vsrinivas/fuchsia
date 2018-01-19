// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stddef.h>

#include <crypto/error.h>
#include <fdio/debug.h>
#include <openssl/cipher.h>
#include <openssl/digest.h>
#include <openssl/err.h>
#include <openssl/hkdf.h>
#include <zircon/errors.h>
#include <zircon/types.h>

#define ZXDEBUG 0

namespace crypto {
namespace {

zx_status_t MapGlobalErrors(int reason) {
    switch (reason) {

    case ERR_R_MALLOC_FAILURE:
        return ZX_ERR_NO_MEMORY;

    case ERR_R_OVERFLOW:
        return ZX_ERR_OUT_OF_RANGE;

    default:
        return ZX_ERR_INTERNAL;
    }
}

zx_status_t MapCipherErrors(int reason) {
    switch (reason) {

    case CIPHER_R_CTRL_NOT_IMPLEMENTED:
    case CIPHER_R_CTRL_OPERATION_NOT_IMPLEMENTED:
    case CIPHER_R_UNSUPPORTED_KEY_SIZE:
    case CIPHER_R_UNSUPPORTED_NONCE_SIZE:
        return ZX_ERR_NOT_SUPPORTED;

    case CIPHER_R_AES_KEY_SETUP_FAILED:
    case CIPHER_R_INITIALIZATION_ERROR:
        return ZX_ERR_NO_RESOURCES;

    case CIPHER_R_BAD_KEY_LENGTH:
    case CIPHER_R_DATA_NOT_MULTIPLE_OF_BLOCK_LENGTH:
    case CIPHER_R_INVALID_NONCE:
    case CIPHER_R_INVALID_NONCE_SIZE:
    case CIPHER_R_INVALID_OPERATION:
    case CIPHER_R_INVALID_KEY_LENGTH:
    case CIPHER_R_INPUT_NOT_INITIALIZED:
    case CIPHER_R_OUTPUT_ALIASES_INPUT:
    case CIPHER_R_TAG_TOO_LARGE:
    case CIPHER_R_TOO_LARGE:
        return ZX_ERR_INVALID_ARGS;

    case CIPHER_R_NO_CIPHER_SET:
    case CIPHER_R_NO_DIRECTION_SET:
    case CIPHER_R_WRONG_FINAL_BLOCK_LENGTH:
        return ZX_ERR_BAD_STATE;

    case CIPHER_R_BUFFER_TOO_SMALL:
        return ZX_ERR_BUFFER_TOO_SMALL;

    case CIPHER_R_BAD_DECRYPT:
        return ZX_ERR_IO_DATA_INTEGRITY;

    default:
        return MapGlobalErrors(reason);
    }
}

zx_status_t MapDigestErrors(int reason) {
    switch (reason) {

    case DIGEST_R_INPUT_NOT_INITIALIZED:
        return ZX_ERR_INVALID_ARGS;

    default:
        return MapGlobalErrors(reason);
    }
}

zx_status_t MapHkdfErrors(int reason) {
    switch (reason) {

    case HKDF_R_OUTPUT_TOO_LARGE:
        return ZX_ERR_BUFFER_TOO_SMALL;

    default:
        return MapGlobalErrors(reason);
    }
}

// Callback to print BoringSSL's error stack
int xprintf_crypto_error(const char* str, size_t len, void* ctx) {
    xprintf("    %s\n", str);
    return 1;
}

} // namespace

void xprintf_crypto_errors(zx_status_t* out) {
    uint32_t packed = ERR_peek_last_error();
    xprintf("BoringSSL error(s):\n");
    ERR_print_errors_cb(xprintf_crypto_error, nullptr);
    if (!out) {
        return;
    }
    int lib = ERR_GET_LIB(packed);
    int reason = ERR_GET_REASON(packed);
    switch (lib) {
    case ERR_R_CIPHER_LIB:
        *out = MapCipherErrors(reason);
        break;
    case ERR_R_DIGEST_LIB:
        *out = MapDigestErrors(reason);
        break;
    case ERR_R_HKDF_LIB:
        *out = MapHkdfErrors(reason);
        break;
    default:
        *out = MapGlobalErrors(reason);
        break;
    }
}

} // namespace crypto
