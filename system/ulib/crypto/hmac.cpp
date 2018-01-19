// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stddef.h>
#include <stdint.h>

#include <crypto/digest.h>
#include <crypto/error.h>
#include <crypto/hmac.h>
#include <explicit-memory/bytes.h>
#include <fbl/alloc_checker.h>
#include <fdio/debug.h>
#include <openssl/digest.h>
#include <openssl/hmac.h>
#include <zircon/errors.h>
#include <zircon/types.h>

#define ZXDEBUG 0

namespace crypto {
namespace {

const uint16_t kAllFlags = HMAC::ALLOW_TRUNCATION | HMAC::ALLOW_WEAK_KEY;
}
// The previously opaque crypto implementation context.  Guaranteed to clean up on destruction.
struct HMAC::Context {
    Context() { HMAC_CTX_init(&impl); }

    ~Context() { HMAC_CTX_cleanup(&impl); }

    HMAC_CTX impl;
};

HMAC::HMAC() {}
HMAC::~HMAC() {}

zx_status_t HMAC::Create(digest::Algorithm digest, const Bytes& key, const void* in, size_t in_len,
                         Bytes* out, uint16_t flags) {
    zx_status_t rc;

    HMAC hmac;
    if ((rc = hmac.Init(digest, key, flags)) != ZX_OK || (rc = hmac.Update(in, in_len)) != ZX_OK ||
        (rc = hmac.Final(out)) != ZX_OK) {
        return rc;
    }

    return ZX_OK;
}

zx_status_t HMAC::Verify(digest::Algorithm digest, const Bytes& key, const void* in, size_t in_len,
                         const Bytes& hmac, uint16_t flags) {
    zx_status_t rc;

    Bytes tmp;
    if ((rc = HMAC::Create(digest, key, in, in_len, &tmp, flags)) != ZX_OK) {
        return rc;
    }

    size_t hmac_len = hmac.len();
    size_t tmp_len = tmp.len();
    if (hmac_len != tmp_len) {
        // According to RFC 2104, section 5, the digest can be truncated to half its original size.
        // We enforce a more stringent minimum than the RFC of 128 bits
        if ((flags & ALLOW_TRUNCATION) == 0 || hmac_len < tmp_len / 2 || hmac_len < 16) {
            xprintf("digest to verify is too short: %zu\n", hmac_len);
            return ZX_ERR_INVALID_ARGS;
        }
        if ((rc = tmp.Resize(hmac.len())) != ZX_OK) {
            return rc;
        }
    }

    if (tmp != hmac) {
        xprintf("HMAC verification failed\n");
        return ZX_ERR_IO_DATA_INTEGRITY;
    }

    return ZX_OK;
}

zx_status_t HMAC::Init(digest::Algorithm digest, const Bytes& key, uint16_t flags) {
    zx_status_t rc;

    if ((flags & (~kAllFlags)) != 0) {
        xprintf("invalid flags: %04x\n", flags);
        return ZX_ERR_INVALID_ARGS;
    }

    fbl::AllocChecker ac;
    ctx_.reset(new (&ac) Context());
    if (!ac.check()) {
        xprintf("allocation failed: %zu bytes\n", sizeof(Context));
        return ZX_ERR_NO_MEMORY;
    }

    // Get the digest algorithm
    uintptr_t ptr;
    if ((rc = digest::GetDigest(digest, &ptr)) != ZX_OK) {
        return rc;
    }
    const EVP_MD* md = reinterpret_cast<const EVP_MD*>(ptr);

    // Check key length.  Keys less than digest length are invalid (RFC 2104, section 2).
    size_t key_len = key.len();
    if ((flags & ALLOW_WEAK_KEY) == 0 && key_len < EVP_MD_size(md)) {
        xprintf("weak key: %zu bytes\n", key_len);
        return ZX_ERR_INVALID_ARGS;
    }

    // Initialize the HMAC context
    if (HMAC_Init_ex(&ctx_->impl, key.get(), key_len, md, nullptr) != 1) {
        xprintf_crypto_errors(&rc);
        return rc;
    }

    return ZX_OK;
}

zx_status_t HMAC::Update(const void* in, size_t in_len) {
    zx_status_t rc;

    if (!ctx_) {
        xprintf("not initialized\n");
        return ZX_ERR_BAD_STATE;
    }

    if (in_len == 0) {
        return ZX_OK;
    }
    if (!in) {
        xprintf("input is null\n");
        return ZX_ERR_INVALID_ARGS;
    }

    if (HMAC_Update(&ctx_->impl, static_cast<const uint8_t*>(in), in_len) != 1) {
        xprintf_crypto_errors(&rc);
        return rc;
    }

    return ZX_OK;
}

zx_status_t HMAC::Final(Bytes* out) {
    zx_status_t rc;

    if (!out) {
        xprintf("null parameter: out\n");
        return ZX_ERR_INVALID_ARGS;
    }

    if (!ctx_) {
        xprintf("not initialized\n");
        return ZX_ERR_BAD_STATE;
    }

    Bytes tmp;
    if ((rc = tmp.Resize(EVP_MAX_MD_SIZE)) != ZX_OK) {
        return rc;
    }

    unsigned int out_len;
    if (HMAC_Final(&ctx_->impl, tmp.get(), &out_len) != 1) {
        xprintf_crypto_errors(&rc);
        return rc;
    }

    out->Reset();
    if ((rc = out->Copy(tmp.get(), out_len)) != ZX_OK) {
        return rc;
    }

    return ZX_OK;
}

} // namespace crypto
