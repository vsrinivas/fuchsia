// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "utils.h"

#include <limits.h>
#include <string.h>
#include <zircon/assert.h>
#include <zircon/types.h>

#include "src/security/lib/fcrypto/aead.h"
#include "src/security/lib/fcrypto/bytes.h"
#include "src/security/lib/fcrypto/cipher.h"
#include "src/security/lib/fcrypto/digest.h"
#include "src/security/lib/fcrypto/hkdf.h"
#include "src/security/lib/fcrypto/hmac.h"

namespace crypto {
namespace testing {

zx_status_t HexToBuf(const char* hex, uint8_t* buf, size_t max) {
  size_t i = 0;
  size_t j = 0;
  uint8_t n;
  while (j < max) {
    char c = hex[i];
    if ('0' <= c && c <= '9') {
      n = static_cast<uint8_t>(c - '0');
    } else if ('a' <= c && c <= 'f') {
      n = static_cast<uint8_t>(c - 'a' + 10);
    } else if ('A' <= c && c <= 'F') {
      n = static_cast<uint8_t>(c - 'A' + 10);
    } else {
      return ZX_ERR_INVALID_ARGS;
    }
    if (i % 2 == 0) {
      buf[j] = static_cast<uint8_t>(n << 4);
    } else {
      buf[j] |= n & 0xF;
      ++j;
    }
    ++i;
  }
  return ZX_OK;
}

zx_status_t HexToBytes(const char* hex, Bytes* out) {
  ZX_DEBUG_ASSERT(hex);
  ZX_DEBUG_ASSERT(out);
  zx_status_t rc;

  size_t len = strlen(hex) / 2;
  if ((rc = out->Resize(len)) != ZX_OK || (rc = HexToBuf(hex, out->get(), len)) != ZX_OK) {
    return rc;
  }

  return ZX_OK;
}

zx_status_t HexToSecret(const char* hex, Secret* out) {
  ZX_DEBUG_ASSERT(hex);
  ZX_DEBUG_ASSERT(out);
  zx_status_t rc;

  uint8_t* buf;
  size_t len = strlen(hex) / 2;
  if ((rc = out->Allocate(len, &buf)) != ZX_OK || (rc = HexToBuf(hex, buf, len)) != ZX_OK) {
    return rc;
  }

  return ZX_OK;
}

zx_status_t GenerateKeyMaterial(Cipher::Algorithm cipher, Secret* key, Bytes* iv) {
  zx_status_t rc;
  ZX_DEBUG_ASSERT(key);

  size_t key_len;
  if ((rc = Cipher::GetKeyLen(cipher, &key_len)) != ZX_OK ||
      (rc = key->Generate(key_len)) != ZX_OK) {
    return rc;
  }
  if (iv) {
    size_t iv_len;
    if ((rc = Cipher::GetIVLen(cipher, &iv_len)) != ZX_OK ||
        (rc = iv->Randomize(iv_len)) != ZX_OK) {
      return rc;
    }
  }

  return ZX_OK;
}

zx_status_t GenerateKeyMaterial(AEAD::Algorithm cipher, Secret* key, Bytes* iv) {
  zx_status_t rc;
  ZX_DEBUG_ASSERT(key);

  size_t key_len;
  if ((rc = AEAD::GetKeyLen(cipher, &key_len)) != ZX_OK || (rc = key->Generate(key_len)) != ZX_OK) {
    return rc;
  }
  if (iv) {
    size_t iv_len;
    if ((rc = AEAD::GetIVLen(cipher, &iv_len)) != ZX_OK || (rc = iv->Randomize(iv_len)) != ZX_OK) {
      return rc;
    }
  }

  return ZX_OK;
}

bool AllEqual(const Bytes& buf, uint8_t val, zx_off_t off, size_t len) {
  for (size_t i = 0; i < len; ++i) {
    if (buf[off] != val) {
      return false;
    }
    off++;
  }
  return true;
}

}  // namespace testing
}  // namespace crypto
