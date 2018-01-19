// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stddef.h>
#include <stdint.h>

#include <crypto/aead.h>
#include <crypto/bytes.h>
#include <crypto/error.h>
#include <fbl/auto_call.h>
#include <fbl/unique_ptr.h>
#include <fdio/debug.h>
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

zx_status_t AEAD::InitSeal(Algorithm aead, const Bytes& key, const Bytes& iv) {
    zx_status_t rc;

    if ((rc = Init(aead, key, Cipher::kEncrypt)) != ZX_OK) {
        return rc;
    }
    if (iv.len() != iv_.len()) {
        xprintf("wrong IV length; have %zu, need %zu\n", iv.len(), iv_.len());
        return ZX_ERR_INVALID_ARGS;
    }
    if ((rc = iv_.Copy(iv)) != ZX_OK) {
        return rc;
    }

    return ZX_OK;
}

zx_status_t AEAD::InitOpen(Algorithm aead, const Bytes& key) {
    return Init(aead, key, Cipher::kDecrypt);
}

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

zx_status_t AEAD::Seal(const Bytes& ptext, Bytes* iv, Bytes* ctext) {
    zx_status_t rc;

    if (direction_ != Cipher::kEncrypt) {
        xprintf("not configured to encrypt\n");
        return ZX_ERR_BAD_STATE;
    }

    size_t ptext_len = ptext.len();
    if (!iv || !ctext) {
        xprintf("bad parameter(s): iv=%p, ctext=%p\n", iv, ctext);
        return ZX_ERR_INVALID_ARGS;
    }

    size_t ctext_len = ptext_len + tag_len_;
    if ((rc = ctext->Resize(ctext_len)) != ZX_OK || (rc = iv->Copy(iv_)) != ZX_OK ||
        (rc = iv_.Increment()) != ZX_OK) {
        return rc;
    }

    size_t out_len;
    if (EVP_AEAD_CTX_seal(&ctx_->impl, ctext->get(), &out_len, ctext_len, iv->get(), iv->len(),
                          ptext.get(), ptext_len, ad_.get(), ad_.len()) != 1) {
        xprintf_crypto_errors(&rc);
        return rc;
    }
    if (out_len != ctext_len) {
        xprintf("length mismatch: expected %zu, got %zu\n", ptext_len, out_len);
        return ZX_ERR_INTERNAL;
    }

    return ZX_OK;
}

zx_status_t AEAD::Open(const Bytes& iv, const Bytes& ctext, Bytes* ptext) {
    zx_status_t rc;

    if (direction_ != Cipher::kDecrypt) {
        xprintf("not configured to decrypt\n");
        return ZX_ERR_BAD_STATE;
    }

    size_t iv_len = iv_.len();
    size_t ctext_len = ctext.len();
    if (iv.len() != iv_len || ctext_len < tag_len_ || !ptext) {
        xprintf("bad parameter(s): iv.len=%zu, ctext.len=%zu, ptext=%p\n", iv.len(), ctext_len,
                ptext);
        return ZX_ERR_INVALID_ARGS;
    }

    size_t ptext_len = ctext_len - tag_len_;
    if ((rc = ptext->Resize(ptext_len)) != ZX_OK) {
        return rc;
    }

    size_t out_len;
    if (EVP_AEAD_CTX_open(&ctx_->impl, ptext->get(), &out_len, ptext_len, iv.get(), iv_len,
                          ctext.get(), ctext_len, ad_.get(), ad_.len()) != 1) {
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
    iv_.Reset();
    ad_.Reset();
    tag_len_ = 0;
}

// Private methods

zx_status_t AEAD::Init(Algorithm algo, const Bytes& key, Cipher::Direction direction) {
    zx_status_t rc;

    Reset();
    auto cleanup = fbl::MakeAutoCall([&] { Reset(); });

    // Look up specific algorithm
    const EVP_AEAD* aead;
    if ((rc = GetAEAD(algo, &aead)) != ZX_OK) {
        return rc;
    }

    // Check parameters
    size_t key_len = EVP_AEAD_key_length(aead);
    if (key.len() != key_len) {
        xprintf("wrong key length; have %zu, need %zu\n", key.len(), key_len);
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

    // Reserve space for IV and auth tag.
    size_t iv_len = EVP_AEAD_nonce_length(aead);
    if ((rc = iv_.Resize(iv_len)) != ZX_OK) {
        return rc;
    }
    tag_len_ = EVP_AEAD_max_tag_len(aead);

    cleanup.cancel();
    return ZX_OK;
}

} // namespace crypto
