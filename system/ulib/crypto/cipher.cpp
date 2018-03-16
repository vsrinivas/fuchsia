// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <inttypes.h>
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

#define ZXDEBUG 0

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
        xprintf("not initialized\n");
        return ZX_ERR_INVALID_ARGS;

    case Cipher::kAES128_CTR:
        *out = EVP_aes_128_ctr();
        return ZX_OK;

    case Cipher::kAES256_XTS:
        *out = EVP_aes_256_xts();
        return ZX_OK;

    default:
        xprintf("invalid cipher = %u\n", cipher);
        return ZX_ERR_NOT_SUPPORTED;
    }
}

} // namespace

// Public methods

zx_status_t Cipher::GetKeyLen(Algorithm algo, size_t* out) {
    zx_status_t rc;

    if (!out) {
        xprintf("missing output pointer\n");
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
        xprintf("missing output pointer\n");
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
        xprintf("missing output pointer\n");
        return ZX_ERR_INVALID_ARGS;
    }
    const EVP_CIPHER* cipher;
    if ((rc = GetCipher(algo, &cipher)) != ZX_OK) {
        return rc;
    }
    *out = cipher->block_size;

    return ZX_OK;
}

Cipher::Cipher() : cipher_(kUninitialized), direction_(kUnset), block_size_(0), alignment_(0) {}

Cipher::~Cipher() {}

zx_status_t Cipher::Init(Algorithm algo, Direction direction, const Bytes& key, const Bytes& iv,
                         uint64_t alignment) {
    zx_status_t rc;

    Reset();
    auto cleanup = fbl::MakeAutoCall([&]() { Reset(); });

    const EVP_CIPHER* cipher;
    if ((rc = GetCipher(algo, &cipher)) != ZX_OK) {
        return rc;
    }
    if (key.len() != cipher->key_len || iv.len() != cipher->iv_len) {
        xprintf("bad parameter(s): key_len=%zu, iv_len=%zu\n", key.len(), iv.len());
        return ZX_ERR_INVALID_ARGS;
    }
    cipher_ = algo;

    // Set the IV.
    if ((rc = iv_.Copy(iv)) != ZX_OK || (rc = tweaked_iv_.Copy(iv)) != ZX_OK) {
        return rc;
    }

    // Handle alignment for random access ciphers
    if (alignment != 0) {
        if ((alignment & (alignment - 1)) != 0) {
            xprintf("alignment must be a power of 2: %" PRIu64 "\n", alignment);
            return ZX_ERR_INVALID_ARGS;
        }
        // Test to make sure we can fully increment the given IV.
        if ((rc = tweaked_iv_.Increment(UINT64_MAX / alignment)) != ZX_OK) {
            return rc;
        }
        // White-list tweaked codebook ciphers
        switch (algo) {
        case kAES128_CTR:
            // !!! WARNING !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
            // TODO(security): ZX-1811.
            // CTR is NOT a tweaked codebook mode, so reusing a nonce and key on two different
            // plain-texts can allow an attacker to "cancel out" the encryption.  Incorrectly
            // marking this as a tweaked mode is a TEMPORARY WORKAROUND to unblock zxcrypt
            // development.  this is not adequate disk encryption and  MUST BE FIXED before zxcrypt
            // can provide reasonable protection to encrypted data.
            // !!! WARNING !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
        case kAES256_XTS:
            break;
        default:
            xprintf("Selected cipher cannot be used in random access mode\n");
            return ZX_ERR_INVALID_ARGS;
        }
    }
    alignment_ = alignment;

    // Initialize cipher context
    fbl::AllocChecker ac;
    ctx_.reset(new (&ac) Context());
    if (!ac.check()) {
        xprintf("allocation failed: %zu bytes\n", sizeof(Context));
        return ZX_ERR_NO_MEMORY;
    }
    if (EVP_CipherInit_ex(&ctx_->impl, cipher, nullptr, key.get(), iv_.get(),
                          direction == kEncrypt) < 0) {
        xprintf_crypto_errors(&rc);
        return rc;
    }
    direction_ = direction;
    block_size_ = cipher->block_size;

    cleanup.cancel();
    return ZX_OK;
}

zx_status_t Cipher::Transform(const uint8_t* in, zx_off_t offset, size_t length, uint8_t* out,
                              Direction direction) {
    zx_status_t rc;

    if (!ctx_ || direction != direction_) {
        xprintf("not initialized/wrong direction\n");
        return ZX_ERR_BAD_STATE;
    }
    if (length == 0) {
        return ZX_OK;
    }
    if (!in || !out || length % block_size_ != 0) {
        xprintf("bad args: in=%p, length=%zu, out=%p, direction=%d\n", in, length, out, direction);
        return ZX_ERR_INVALID_ARGS;
    }
    if (alignment_ == 0) {
        // Stream cipher; just transform without modifying the IV.
        if (EVP_Cipher(&ctx_->impl, out, in, length) <= 0) {
            xprintf_crypto_errors(&rc);
            return rc;
        }

    } else {
        if (offset % alignment_ != 0) {
            xprintf("unaligned offset\n");
            return ZX_ERR_INVALID_ARGS;
        }
        if ((rc = tweaked_iv_.Copy(iv_)) != ZX_OK ||
            (rc = tweaked_iv_.Increment(offset / alignment_)) != ZX_OK) {
            return rc;
        }
        while (length > 0) {
            if (EVP_CipherInit_ex(&ctx_->impl, nullptr, nullptr, nullptr, tweaked_iv_.get(), -1) <
                0) {
                xprintf_crypto_errors(&rc);
                return rc;
            }
            size_t chunk_len = length < alignment_ ? length : alignment_;
            int res;
            if ((res = EVP_Cipher(&ctx_->impl, out, in, chunk_len)) <= 0) {
                xprintf_crypto_errors(&rc);
                return rc;
            }
            out += chunk_len;
            in += chunk_len;
            length -= chunk_len;
            if ((rc = tweaked_iv_.Increment()) != ZX_OK) {
                return rc;
            }
        }
    }

    return ZX_OK;
}

void Cipher::Reset() {
    ctx_.reset();
    block_size_ = 0;
    cipher_ = kUninitialized;
    direction_ = kUnset;
    iv_.Reset();
    tweaked_iv_.Reset();
    alignment_ = 0;
}

} // namespace crypto
