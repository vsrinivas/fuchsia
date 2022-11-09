// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zircon-internal/debug.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <zircon/assert.h>
#include <zircon/errors.h>
#include <zircon/types.h>

#include <explicit-memory/bytes.h>

// See note in //zircon/third_party/ulib/boringssl/BUILD.gn
#define BORINGSSL_NO_CXX
#include <openssl/digest.h>
#include <openssl/hkdf.h>

#include "src/security/lib/fcrypto/digest.h"
#include "src/security/lib/fcrypto/error.h"
#include "src/security/lib/fcrypto/hkdf.h"

#define ZXDEBUG 0

namespace crypto {
namespace {

const uint16_t kAllFlags = HKDF::ALLOW_WEAK_KEY;
}
// Public methods

HKDF::HKDF() : digest_(digest::kUninitialized) {}
HKDF::~HKDF() {}

zx_status_t HKDF::Init(digest::Algorithm digest, const Secret& key, const Bytes& salt,
                       uint16_t flags) {
  zx_status_t rc;

  if ((flags & (~kAllFlags)) != 0) {
    xprintf("invalid flags: %04x\n", flags);
    return ZX_ERR_INVALID_ARGS;
  }

  uintptr_t ptr;
  if ((rc = digest::GetDigest(digest, &ptr)) != ZX_OK) {
    return rc;
  }
  digest_ = digest;
  const EVP_MD* md = reinterpret_cast<const EVP_MD*>(ptr);

  // Reserve space for the pseudo-random key.
  uint8_t* prk;
  size_t prk_len = EVP_MD_size(md);
  if ((rc = prk_.Allocate(prk_len, &prk)) != ZX_OK) {
    return rc;
  }

  // Recommended minimum length for the key is the digest output length (RFC 2104, section 2).
  if ((flags & ALLOW_WEAK_KEY) == 0 && key.len() < prk_len) {
    xprintf("weak parameter(s): key_len=%zu", key.len());
    return ZX_ERR_INVALID_ARGS;
  }

  // Extract the PRK used to generate other keys.
  if (HKDF_extract(prk, &prk_len, md, key.get(), key.len(), salt.get(), salt.len()) < 0) {
    xprintf_crypto_errors(&rc);
    return rc;
  }
  ZX_DEBUG_ASSERT(prk_len == prk_.len());

  return ZX_OK;
}

zx_status_t HKDF::Derive(const char* label, size_t len, Bytes* out) {
  zx_status_t rc;

  if ((rc = out->Resize(len)) != ZX_OK || (rc = Derive(label, out->get(), len)) != ZX_OK) {
    return rc;
  }

  return ZX_OK;
}

zx_status_t HKDF::Derive(const char* label, size_t len, Secret* out) {
  zx_status_t rc;

  uint8_t* buf;
  if ((rc = out->Allocate(len, &buf)) != ZX_OK || (rc = Derive(label, buf, len)) != ZX_OK) {
    return rc;
  }

  return ZX_OK;
}

zx_status_t HKDF::Derive(const char* label, uint8_t* out, size_t out_len) {
  zx_status_t rc;

  if (!out || out_len == 0) {
    xprintf("bad parameter(s): out=%p, out_len=%zu\n", out, out_len);
    return ZX_ERR_INVALID_ARGS;
  }

  uintptr_t ptr;
  if ((rc = digest::GetDigest(digest_, &ptr)) != ZX_OK) {
    return rc;
  }
  const EVP_MD* md = reinterpret_cast<const EVP_MD*>(ptr);

  // Maybe the platform is weird...
  static_assert(sizeof(uint8_t) == sizeof(char), "can't convert char to uint8_t");
  const uint8_t* info = reinterpret_cast<const uint8_t*>(label);
  size_t info_len = label ? strlen(label) : 0;

  // Generate the key
  if (HKDF_expand(out, out_len, md, prk_.get(), prk_.len(), info, info_len) < 0) {
    xprintf_crypto_errors(&rc);
    return rc;
  }

  return ZX_OK;
}

}  // namespace crypto
