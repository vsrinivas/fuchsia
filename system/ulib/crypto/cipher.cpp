// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <crypto/bytes.h>
#include <crypto/cipher.h>
#include <crypto/error.h>
#include <explicit-memory/bytes.h>
#include <fbl/algorithm.h>
#include <fbl/auto_call.h>
#include <fbl/macros.h>
#include <fbl/unique_ptr.h>
#include <fdio/debug.h>
#include <openssl/cipher.h>
#include <zircon/errors.h>
#include <zircon/types.h>

#define MXDEBUG 0

namespace crypto {

// The previously opaque crypto implementation context.  Guaranteed to clean up on destruction.
struct Cipher::Context {
    Context() { EVP_CIPHER_CTX_init(&impl); }

    ~Context() { EVP_CIPHER_CTX_cleanup(&impl); }

    EVP_CIPHER_CTX impl;
};

namespace {

// Get the cipher for the given |version|.
zx_status_t GetCipher(Cipher::Algorithm cipher, const EVP_CIPHER** out) {
    switch (cipher) {
    case Cipher::kUninitialized:
        xprintf("%s: not initialized\n", __PRETTY_FUNCTION__);
        return ZX_ERR_INVALID_ARGS;

    case Cipher::kAES256_XTS:
        *out = EVP_aes_256_xts();
        return ZX_OK;

    default:
        xprintf("%s: invalid cipher = %u\n", __PRETTY_FUNCTION__, cipher);
        return ZX_ERR_NOT_SUPPORTED;
    }
}

} // namespace

// Public methods

zx_status_t Cipher::GetKeyLen(Algorithm algo, size_t* out) {
    zx_status_t rc;

    if (!out) {
        xprintf("%s: missing output pointer\n", __PRETTY_FUNCTION__);
        return ZX_ERR_INVALID_ARGS;
    }
    const EVP_CIPHER* cipher;
    if ((rc = GetCipher(algo, &cipher)) != ZX_OK) {
        return rc;
    }
    *out = cipher->key_len;

    return ZX_OK;
}

zx_status_t Cipher::GetIVLen(Algorithm algo, size_t* out) {
    zx_status_t rc;

    if (!out) {
        xprintf("%s: missing output pointer\n", __PRETTY_FUNCTION__);
        return ZX_ERR_INVALID_ARGS;
    }
    const EVP_CIPHER* cipher;
    if ((rc = GetCipher(algo, &cipher)) != ZX_OK) {
        return rc;
    }
    *out = cipher->iv_len;

    return ZX_OK;
}

zx_status_t Cipher::GetBlockSize(Algorithm algo, size_t* out) {
    zx_status_t rc;

    if (!out) {
        xprintf("%s: missing output pointer\n", __PRETTY_FUNCTION__);
        return ZX_ERR_INVALID_ARGS;
    }
    const EVP_CIPHER* cipher;
    if ((rc = GetCipher(algo, &cipher)) != ZX_OK) {
        return rc;
    }
    *out = cipher->block_size;

    return ZX_OK;
}

Cipher::Cipher()
    : ctx_(nullptr), cipher_(kUninitialized), direction_(kUnset), block_size_(0), tweakable_(0) {}
Cipher::~Cipher() {}

zx_status_t Cipher::InitEncrypt(Algorithm cipher, const Bytes& key, const Bytes& iv,
                                uint64_t tweakable) {
    return Init(cipher, key, iv, tweakable, kEncrypt);
}

zx_status_t Cipher::InitDecrypt(Algorithm cipher, const Bytes& key, const Bytes& iv,
                                uint64_t tweakable) {
    return Init(cipher, key, iv, tweakable, kDecrypt);
}

zx_status_t Cipher::GetDirection(Direction* out) const {
    if (!ctx_) {
        xprintf("%s: not initialized\n", __PRETTY_FUNCTION__);
        return ZX_ERR_BAD_STATE;
    }
    if (!out) {
        xprintf("%s: missing output pointer\n", __PRETTY_FUNCTION__);
        return ZX_ERR_INVALID_ARGS;
    }
    *out = direction_;
    return ZX_OK;
}

zx_status_t Cipher::Tweak(uint64_t offset) {
    zx_status_t rc;

    if (!ctx_) {
        xprintf("%s: not initialized\n", __PRETTY_FUNCTION__);
        return ZX_ERR_BAD_STATE;
    }
    if (offset > tweakable_) {
        xprintf("%s: invalid offset: have %lu, max is %lu\n", __PRETTY_FUNCTION__, offset,
                tweakable_);
        return ZX_ERR_INVALID_ARGS;
    }
    uint64_t tweak;
    memcpy(&tweak, iv_.get(), sizeof(tweak));
    tweak &= ~tweakable_;
    tweak |= offset;
    if ((rc = iv_.Copy(&tweak, sizeof(tweak))) != ZX_OK) {
        return rc;
    }
    if (EVP_CipherInit_ex(&ctx_->impl, nullptr, nullptr, nullptr, iv_.get(), -1) < 0) {
        xprintf_crypto_errors(__PRETTY_FUNCTION__, &rc);
        return rc;
    }

    return ZX_OK;
}

zx_status_t Cipher::Encrypt(const uint8_t* in, size_t len, uint8_t* out) {
    if (direction_ != kEncrypt) {
        xprintf("%s: wrong direction: %u\n", __PRETTY_FUNCTION__, direction_);
        return ZX_ERR_BAD_STATE;
    }
    return Transform(in, len, out);
}

zx_status_t Cipher::Decrypt(const uint8_t* in, size_t len, uint8_t* out) {
    if (direction_ != kDecrypt) {
        xprintf("%s: wrong direction: %u\n", __PRETTY_FUNCTION__, direction_);
        return ZX_ERR_BAD_STATE;
    }
    return Transform(in, len, out);
}

void Cipher::Reset() {
    ctx_.reset();
    cipher_ = kUninitialized;
    direction_ = kUnset;
    iv_.Reset();
    block_size_ = 0;
    tweakable_ = 0;
}

// Private methods

zx_status_t Cipher::Init(Algorithm algo, const Bytes& key, const Bytes& iv, uint64_t tweakable,
                         Direction direction) {
    zx_status_t rc;

    Reset();
    auto cleanup = fbl::MakeAutoCall([&]() { Reset(); });

    const EVP_CIPHER* cipher;
    if ((rc = GetCipher(algo, &cipher)) != ZX_OK) {
        return rc;
    }
    if (key.len() != cipher->key_len || iv.len() != cipher->iv_len) {
        xprintf("%s: bad parameter(s): key_len=%zu, iv_len=%zu\n", __PRETTY_FUNCTION__, key.len(),
                iv.len());
        return ZX_ERR_INVALID_ARGS;
    }
    cipher_ = algo;

    // Set the IV.  This may be adjusted in |Tweak| is |tweakable| is nonzero.
    if ((rc = iv_.Copy(iv.get(), iv.len())) != ZX_OK) {
        return rc;
    }
    if (cipher->iv_len < sizeof(tweakable)) {
        xprintf("%s: IV is too small: %u\n", __PRETTY_FUNCTION__, cipher->iv_len);
        return ZX_ERR_NOT_SUPPORTED;
    }
    if (((tweakable + 1) & tweakable) != 0) {
        xprintf("%s: tweakable bit mask must be of the form '(2^n)-1': %08lx\n",
                __PRETTY_FUNCTION__, tweakable);
        return ZX_ERR_INVALID_ARGS;
    }
    tweakable_ = tweakable;

    // Initialize cipher context
    fbl::AllocChecker ac;
    ctx_.reset(new (&ac) Context());
    if (!ac.check()) {
        xprintf("%s: allocation failed: %zu bytes\n", __PRETTY_FUNCTION__, sizeof(Context));
        return ZX_ERR_NO_MEMORY;
    }
    if (EVP_CipherInit_ex(&ctx_->impl, cipher, nullptr, key.get(), iv_.get(),
                          direction == kEncrypt) < 0) {
        xprintf_crypto_errors(__PRETTY_FUNCTION__, &rc);
        return rc;
    }
    direction_ = direction;
    block_size_ = cipher->block_size;

    cleanup.cancel();
    return ZX_OK;
}

zx_status_t Cipher::Transform(const uint8_t* in, size_t len, uint8_t* out) {
    zx_status_t rc;

    if (!ctx_) {
        xprintf("%s: not initialized\n", __PRETTY_FUNCTION__);
        return ZX_ERR_BAD_STATE;
    }
    if (len == 0) {
        return ZX_OK;
    }
    if (!in || !out || len % block_size_ != 0) {
        xprintf("%s: bad args: in=%p, len=%zu, out=%p\n", __PRETTY_FUNCTION__, in, len, out);
        return ZX_ERR_INVALID_ARGS;
    }
    if (EVP_Cipher(&ctx_->impl, out, in, len) <= 0) {
        xprintf_crypto_errors(__PRETTY_FUNCTION__, &rc);
        return rc;
    }
    return ZX_OK;
}

} // namespace crypto
