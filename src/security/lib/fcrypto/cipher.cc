// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <inttypes.h>
#include <lib/fit/defer.h>
#include <lib/zircon-internal/debug.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <zircon/errors.h>
#include <zircon/types.h>

#include <explicit-memory/bytes.h>
#include <fbl/algorithm.h>
#include <fbl/alloc_checker.h>
#include <fbl/macros.h>

// See note in //zircon/third_party/ulib/boringssl/BUILD.gn
#define BORINGSSL_NO_CXX
#include <openssl/cipher.h>

#include "src/security/lib/fcrypto/bytes.h"
#include "src/security/lib/fcrypto/cipher.h"
#include "src/security/lib/fcrypto/error.h"

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

    case Cipher::kAES256_XTS:
      *out = EVP_aes_256_xts();
      return ZX_OK;

    default:
      xprintf("invalid cipher = %u\n", cipher);
      return ZX_ERR_NOT_SUPPORTED;
  }
}

}  // namespace

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
  *out = EVP_CIPHER_key_length(cipher);

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
  *out = EVP_CIPHER_iv_length(cipher);

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
  *out = EVP_CIPHER_block_size(cipher);

  return ZX_OK;
}

Cipher::Cipher() : cipher_(kUninitialized), direction_(kUnset), block_size_(0), alignment_(0) {}

Cipher::~Cipher() {}

zx_status_t Cipher::Init(Algorithm algo, Direction direction, const Secret& key, const Bytes& iv,
                         uint64_t alignment) {
  zx_status_t rc;

  Reset();
  auto cleanup = fit::defer([&]() { Reset(); });

  const EVP_CIPHER* cipher;
  if ((rc = GetCipher(algo, &cipher)) != ZX_OK) {
    return rc;
  }
  if (key.len() != EVP_CIPHER_key_length(cipher) || iv.len() != EVP_CIPHER_iv_length(cipher)) {
    xprintf("bad parameter(s): key_len=%zu, iv_len=%zu\n", key.len(), iv.len());
    return ZX_ERR_INVALID_ARGS;
  }
  cipher_ = algo;

  // Set the IV.
  fbl::AllocChecker ac;
  size_t n = fbl::round_up(EVP_CIPHER_iv_length(cipher), sizeof(zx_off_t)) / sizeof(zx_off_t);
  iv_.reset(new (&ac) zx_off_t[n]{0});
  if (!ac.check()) {
    xprintf("failed to allocate %zu bytes\n", n * sizeof(zx_off_t));
    return ZX_ERR_NO_MEMORY;
  }
  memcpy(iv_.get(), iv.get(), iv.len());
  iv0_ = iv_[0];

  // Handle alignment for random access ciphers
  if (alignment != 0) {
    if ((alignment & (alignment - 1)) != 0) {
      xprintf("alignment must be a power of 2: %" PRIu64 "\n", alignment);
      return ZX_ERR_INVALID_ARGS;
    }
    // White-list tweaked codebook ciphers
    switch (algo) {
      case kAES256_XTS:
        break;
      default:
        xprintf("Selected cipher cannot be used in random access mode\n");
        return ZX_ERR_INVALID_ARGS;
    }
  }
  alignment_ = alignment;

  // Initialize cipher context
  ctx_.reset(new (&ac) Context());
  if (!ac.check()) {
    xprintf("allocation failed: %zu bytes\n", sizeof(Context));
    return ZX_ERR_NO_MEMORY;
  }
  uint8_t* iv8 = reinterpret_cast<uint8_t*>(iv_.get());
  if (EVP_CipherInit_ex(&ctx_->impl, cipher, nullptr, key.get(), iv8, direction == kEncrypt) < 0) {
    xprintf_crypto_errors(&rc);
    return rc;
  }
  direction_ = direction;
  block_size_ = EVP_CIPHER_block_size(cipher);

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
    iv_[0] = iv0_ + static_cast<uint64_t>(offset / alignment_);
    uint8_t* iv8 = reinterpret_cast<uint8_t*>(iv_.get());
    while (length > 0) {
      size_t chunk_len = length < alignment_ ? length : alignment_;
      if (EVP_CipherInit_ex(&ctx_->impl, nullptr, nullptr, nullptr, iv8, -1) < 0 ||
          EVP_Cipher(&ctx_->impl, out, in, chunk_len) <= 0) {
        xprintf_crypto_errors(&rc);
        return rc;
      }
      out += chunk_len;
      in += chunk_len;
      length -= chunk_len;
      iv_[0] += 1;
    }
  }

  return ZX_OK;
}

void Cipher::Reset() {
  ctx_.reset();
  block_size_ = 0;
  cipher_ = kUninitialized;
  direction_ = kUnset;
  alignment_ = 0;
}

}  // namespace crypto
