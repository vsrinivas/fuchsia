// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/security/fcrypto-rust/ffi.h"

#include <memory>

#include "src/security/fcrypto/bytes.h"
#include "src/security/fcrypto/cipher.h"
#include "src/security/fcrypto/secret.h"

namespace crypto {

std::unique_ptr<Cipher> new_cipher() { return std::make_unique<Cipher>(); }

static zx_status_t init_internal(Cipher& cipher, Cipher::Direction direction,
                                 rust::Slice<const uint8_t>& secret, rust::Slice<const uint8_t>& iv,
                                 uint64_t alignment) {
  // Allocate Secret, Bytes
  Secret crypto_secret;
  zx_status_t rc;
  uint8_t* secret_inner;
  rc = crypto_secret.Allocate(secret.length(), &secret_inner);
  if (rc != ZX_OK) {
    return rc;
  }

  Bytes crypto_iv;
  rc = crypto_iv.Resize(iv.length());
  if (rc != ZX_OK) {
    return rc;
  }

  // Populate the buffers.
  memcpy(secret_inner, secret.data(), secret.length());
  memcpy(crypto_iv.get(), iv.data(), iv.length());

  return cipher.Init(crypto::Cipher::Algorithm::kAES256_XTS, direction, crypto_secret, crypto_iv,
                     alignment);
}

zx_status_t init_for_encipher(Cipher& cipher, rust::Slice<const uint8_t> secret,
                              rust::Slice<const uint8_t> iv, uint64_t alignment) {
  return init_internal(cipher, Cipher::Direction::kEncrypt, secret, iv, alignment);
}

zx_status_t init_for_decipher(Cipher& cipher, rust::Slice<const uint8_t> secret,
                              rust::Slice<const uint8_t> iv, uint64_t alignment) {
  return init_internal(cipher, Cipher::Direction::kDecrypt, secret, iv, alignment);
}

zx_status_t encipher(Cipher& cipher, rust::Slice<const uint8_t> plaintext, uint64_t offset,
                     rust::Slice<uint8_t> ciphertext) {
  if (plaintext.length() != ciphertext.length()) {
    return ZX_ERR_INVALID_ARGS;
  }
  return cipher.Encrypt(plaintext.data(), offset, plaintext.length(), ciphertext.data());
}

zx_status_t decipher(Cipher& cipher, rust::Slice<const uint8_t> ciphertext, uint64_t offset,
                     rust::Slice<uint8_t> plaintext) {
  if (plaintext.length() != ciphertext.length()) {
    return ZX_ERR_INVALID_ARGS;
  }
  return cipher.Decrypt(ciphertext.data(), offset, ciphertext.length(), plaintext.data());
}

}  //  namespace crypto
