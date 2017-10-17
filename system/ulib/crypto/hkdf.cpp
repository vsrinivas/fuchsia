// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <crypto/digest.h>
#include <crypto/error.h>
#include <crypto/hkdf.h>
#include <explicit-memory/bytes.h>
#include <fbl/auto_call.h>
#include <fdio/debug.h>
#include <openssl/digest.h>
#include <openssl/hkdf.h>
#include <zircon/errors.h>
#include <zircon/types.h>

#define MXDEBUG 0

namespace crypto {
namespace {

const uint16_t kAllFlags = HKDF::ALLOW_WEAK_KEY;

}
// Public methods

HKDF::HKDF() : digest_(digest::kUninitialized) {}
HKDF::~HKDF() {}

zx_status_t HKDF::Init(digest::Algorithm digest, const Bytes& key, const Bytes& salt,
                       uint16_t flags) {
    zx_status_t rc;

    if ((flags & (~kAllFlags)) != 0) {
        xprintf("%s: invalid flagsL %04x\n", __PRETTY_FUNCTION__, flags);
        return ZX_ERR_INVALID_ARGS;
    }

    Bytes prk;
    uintptr_t ptr;
    if ((rc = prk.Resize(EVP_MAX_MD_SIZE)) != ZX_OK ||
        (rc = digest::GetDigest(digest, &ptr)) != ZX_OK) {
        return rc;
    }
    const EVP_MD* md = reinterpret_cast<const EVP_MD*>(ptr);

    // Recommended minimum length for the key is the digest output length (RFC 2104, section 2).
    if ((flags & ALLOW_WEAK_KEY) == 0 && key.len() < EVP_MD_size(md)) {
        xprintf("%s: weak parameter(s): key_len=%zu", __PRETTY_FUNCTION__, key.len());
        return ZX_ERR_INVALID_ARGS;
    }

    // Extract the PRK used to generate other keys.
    size_t prk_len;
    if (HKDF_extract(prk.get(), &prk_len, md, key.get(), key.len(), salt.get(), salt.len()) < 0) {
        xprintf_crypto_errors(__PRETTY_FUNCTION__, &rc);
        return rc;
    }

    digest_ = digest;
    prk_.Reset();
    return prk_.Copy(prk.get(), prk_len);
}

zx_status_t HKDF::Derive(const char* label, Bytes* out_key) {
    zx_status_t rc;

    uintptr_t ptr;
    if ((rc = digest::GetDigest(digest_, &ptr)) != ZX_OK) {
        return rc;
    }
    const EVP_MD* md = reinterpret_cast<const EVP_MD*>(ptr);

    if (!out_key || out_key->len() == 0) {
        xprintf("%s: bad parameter(s): out_key=%p\n", __PRETTY_FUNCTION__, out_key);
        return ZX_ERR_INVALID_ARGS;
    }

    // Maybe the platform is weird...
    static_assert(sizeof(uint8_t) == sizeof(char), "can't convert char to uint8_t");
    const uint8_t* info = reinterpret_cast<const uint8_t*>(label);
    size_t len = label ? strlen(label) : 0;

    // Generate the key
    if (HKDF_expand(out_key->get(), out_key->len(), md, prk_.get(), prk_.len(), info, len) < 0) {
        xprintf_crypto_errors(__PRETTY_FUNCTION__, &rc);
        return rc;
    }
    return ZX_OK;
}

} // namespace crypto
