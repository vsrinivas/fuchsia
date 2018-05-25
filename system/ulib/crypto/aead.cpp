// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <crypto/aead.h>
#include <crypto/bytes.h>
#include <crypto/error.h>
#include <fbl/algorithm.h>
#include <fbl/auto_call.h>
#include <fbl/unique_ptr.h>
#include <lib/fdio/debug.h>
#include <openssl/aead.h>
#include <zircon/errors.h>
#include <zircon/types.h>

#define ZXDEBUG 0

namespace crypto {

// The previously opaque crypto implementation context.  Guaranteed to clean up on destruction.
struct AEAD::Context {
    Context() {}

    ~Context() { EVP_AEAD_CTX_cleanup(&impl); }

    EVP_AEAD_CTX impl;
};

namespace {

// Get the aead for the given |version|.
zx_status_t GetAEAD(AEAD::Algorithm aead, const EVP_AEAD** out) {
    switch (aead) {
    case AEAD::kUninitialized:
        xprintf("not initialized\n");
        return ZX_ERR_INVALID_ARGS;

    case AEAD::kAES128_GCM:
        *out = EVP_aead_aes_128_gcm();
        return ZX_OK;

    case AEAD::kAES128_GCM_SIV:
        *out = EVP_aead_aes_128_gcm_siv();
        return ZX_OK;

    default:
        xprintf("invalid aead = %u\n", aead);
        return ZX_ERR_NOT_SUPPORTED;
    }
}

} // namespace

// Public methods

zx_status_t AEAD::GetKeyLen(Algorithm algo, size_t* out) {
    zx_status_t rc;

    if (!out) {
        xprintf("missing output pointer\n");
        return ZX_ERR_INVALID_ARGS;
    }
    const EVP_AEAD* aead;
    if ((rc = GetAEAD(algo, &aead)) != ZX_OK) {
        return rc;
    }
    *out = EVP_AEAD_key_length(aead);

    return ZX_OK;
}

zx_status_t AEAD::GetIVLen(Algorithm algo, size_t* out) {
    zx_status_t rc;

    if (!out) {
        xprintf("missing output pointer\n");
        return ZX_ERR_INVALID_ARGS;
    }
    const EVP_AEAD* aead;
    if ((rc = GetAEAD(algo, &aead)) != ZX_OK) {
        return rc;
    }
    *out = EVP_AEAD_nonce_length(aead);

    return ZX_OK;
}

zx_status_t AEAD::GetTagLen(Algorithm algo, size_t* out) {
    zx_status_t rc;

    if (!out) {
        xprintf("missing output pointer\n");
        return ZX_ERR_INVALID_ARGS;
    }
    const EVP_AEAD* aead;
    if ((rc = GetAEAD(algo, &aead)) != ZX_OK) {
        return rc;
    }
    *out = EVP_AEAD_max_tag_len(aead);

    return ZX_OK;
}

AEAD::AEAD() : ctx_(nullptr), direction_(Cipher::kUnset), tag_len_(0) {}

AEAD::~AEAD() {}

zx_status_t AEAD::SetAD(const Bytes& ad) {
    zx_status_t rc;

    if (direction_ == Cipher::kUnset) {
        xprintf("not configured\n");
        return ZX_ERR_BAD_STATE;
    }

    ad_.Reset();
    if ((rc = ad_.Copy(ad)) != ZX_OK) {
        return rc;
    }

    return ZX_OK;
}

zx_status_t AEAD::AllocAD(size_t ad_len, uintptr_t* out_ad) {
    zx_status_t rc;

    if (direction_ == Cipher::kUnset) {
        xprintf("not configured\n");
        return ZX_ERR_BAD_STATE;
    }

    ad_.Reset();
    if ((rc = ad_.Resize(ad_len)) != ZX_OK) {
        return rc;
    }

    if (out_ad) {
        *out_ad = reinterpret_cast<uintptr_t>(ad_.get());
    }
    return ZX_OK;
}

zx_status_t AEAD::Seal(const Bytes& ptext, uint64_t* out_nonce, Bytes* ctext) {
    zx_status_t rc;

    if (direction_ != Cipher::kEncrypt) {
        xprintf("not configured to encrypt\n");
        return ZX_ERR_BAD_STATE;
    }

    size_t ptext_len = ptext.len();
    if (!out_nonce || !ctext) {
        xprintf("bad parameter(s): out_nonce=%p, ctext=%p\n", out_nonce, ctext);
        return ZX_ERR_INVALID_ARGS;
    }

    // If the caller recycles the |Bytes| used for|ctext|, this becomes a no-op.
    size_t ctext_len = ptext_len + tag_len_;
    if ((rc = ctext->Resize(ctext_len)) != ZX_OK) {
        return rc;
    }

    uint8_t* iv8 = reinterpret_cast<uint8_t*>(iv_.get());
    size_t out_len;
    if (EVP_AEAD_CTX_seal(&ctx_->impl, ctext->get(), &out_len, ctext_len, iv8, iv_len_, ptext.get(),
                          ptext_len, ad_.get(), ad_.len()) != 1) {
        xprintf_crypto_errors(&rc);
        return rc;
    }
    if (out_len != ctext_len) {
        xprintf("length mismatch: expected %zu, got %zu\n", ptext_len, out_len);
        return ZX_ERR_INTERNAL;
    }

    // Increment nonce
    uint64_t nonce = iv_[0];
    iv_[0] += 1;
    if (iv_[0] == iv0_) {
        xprintf("exceeded maximum operations with this key\n");
        return ZX_ERR_BAD_STATE;
    }

    *out_nonce = nonce;
    return ZX_OK;
}

zx_status_t AEAD::Open(uint64_t nonce, const Bytes& ctext, Bytes* ptext) {
    zx_status_t rc;

    if (direction_ != Cipher::kDecrypt) {
        xprintf("not configured to decrypt\n");
        return ZX_ERR_BAD_STATE;
    }

    size_t ctext_len = ctext.len();
    if (ctext_len < tag_len_ || !ptext) {
        xprintf("bad parameter(s): ctext.len=%zu, ptext=%p\n", ctext_len, ptext);
        return ZX_ERR_INVALID_ARGS;
    }

    size_t ptext_len = ctext_len - tag_len_;
    if ((rc = ptext->Resize(ptext_len)) != ZX_OK) {
        return rc;
    }

    // Inject nonce
    iv_[0] = nonce;
    uint8_t* iv8 = reinterpret_cast<uint8_t*>(iv_.get());
    size_t out_len;
    if (EVP_AEAD_CTX_open(&ctx_->impl, ptext->get(), &out_len, ptext_len, iv8, iv_len_, ctext.get(),
                          ctext_len, ad_.get(), ad_.len()) != 1) {
        xprintf_crypto_errors(&rc);
        return rc;
    }
    if (out_len != ptext_len) {
        xprintf("length mismatch: expected %zu, got %zu\n", ptext_len, out_len);
        return ZX_ERR_INTERNAL;
    }

    return ZX_OK;
}

void AEAD::Reset() {
    ctx_.reset();
    direction_ = Cipher::kUnset;
    iv_len_ = 0;
    ad_.Reset();
    tag_len_ = 0;
}

// Private methods

zx_status_t AEAD::Init(Algorithm algo, const Bytes& key, const Bytes& iv,
                       Cipher::Direction direction) {
    zx_status_t rc;

    Reset();
    auto cleanup = fbl::MakeAutoCall([&] { Reset(); });

    // Look up specific algorithm
    const EVP_AEAD* aead;
    if ((rc = GetAEAD(algo, &aead)) != ZX_OK) {
        return rc;
    }
    size_t key_len = EVP_AEAD_key_length(aead);
    iv_len_ = EVP_AEAD_nonce_length(aead);
    tag_len_ = EVP_AEAD_max_tag_len(aead);

    // Check parameters
    if (key.len() != key_len) {
        xprintf("wrong key length; have %zu, need %zu\n", key.len(), key_len);
        return ZX_ERR_INVALID_ARGS;
    }
    if (iv.len() != iv_len_) {
        xprintf("wrong IV length; have %zu, need %zu\n", iv.len(), iv_len_);
        return ZX_ERR_INVALID_ARGS;
    }

    // Allocate context
    fbl::AllocChecker ac;
    ctx_.reset(new (&ac) Context());
    if (!ac.check()) {
        xprintf("allocation failed: %zu bytes\n", sizeof(Context));
        return ZX_ERR_NO_MEMORY;
    }

    // Initialize context
    if (EVP_AEAD_CTX_init(&ctx_->impl, aead, key.get(), key.len(), EVP_AEAD_DEFAULT_TAG_LENGTH,
                          nullptr) != 1) {
        xprintf_crypto_errors(&rc);
        return rc;
    }
    direction_ = direction;

    // Reserve space for IV
    size_t n = fbl::round_up(iv_len_, sizeof(uint64_t)) / sizeof(uint64_t);
    iv_.reset(new (&ac) uint64_t[n]{0});
    if (!ac.check()) {
        xprintf("failed to allocate %zu bytes\n", n * sizeof(uint64_t));
        return ZX_ERR_NO_MEMORY;
    }
    memcpy(iv_.get(), iv.get(), iv_len_);

    cleanup.cancel();
    return ZX_OK;
}

} // namespace crypto
