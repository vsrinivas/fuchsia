// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/encryption/primitives/encrypt.h"

#include <openssl/aead.h>
#include <zircon/syscalls.h>

namespace encryption {

bool AES128GCMSIVEncrypt(fxl::StringView key, fxl::StringView data,
                         std::string* output) {
  if (key.size() != 16) {
    return false;
  }

  const EVP_AEAD* algorithm = EVP_aead_aes_128_gcm_siv();

  const size_t kNonceOffset = 0;
  const size_t kNonceSize = EVP_AEAD_nonce_length(algorithm);
  const size_t kTagOffset = kNonceOffset + kNonceSize;
  const size_t kTagSize = EVP_AEAD_max_tag_len(algorithm);
  const size_t kEncryptedDataOffset = kTagOffset + kTagSize;

  bssl::ScopedEVP_AEAD_CTX ctx;
  if (!EVP_AEAD_CTX_init_with_direction(
          ctx.get(), algorithm, reinterpret_cast<const uint8_t*>(key.data()),
          key.size(), EVP_AEAD_DEFAULT_TAG_LENGTH, evp_aead_seal)) {
    return false;
  }
  std::string result;
  result.resize(kEncryptedDataOffset + data.size() +
                EVP_AEAD_max_overhead(algorithm));
  uint8_t* result_as_int8_ptr = reinterpret_cast<uint8_t*>(&result[0]);

  // Generate seed.
  zx_cprng_draw(&result[0], EVP_AEAD_nonce_length(algorithm));

  size_t out_len;
  if (EVP_AEAD_CTX_seal(
          ctx.get(), &result_as_int8_ptr[kEncryptedDataOffset], &out_len,
          data.size() + EVP_AEAD_max_overhead(algorithm),
          &result_as_int8_ptr[kNonceOffset], kNonceSize,
          reinterpret_cast<const uint8_t*>(data.data()), data.size(),
          &result_as_int8_ptr[kTagOffset], kTagSize) == 0) {
    return false;
  }
  result.resize(kEncryptedDataOffset + out_len);
  output->swap(result);
  return true;
}

bool AES128GCMSIVDecrypt(fxl::StringView key, fxl::StringView encrypted_data,
                         std::string* output) {
  if (key.size() != 16) {
    return false;
  }

  const EVP_AEAD* algorithm = EVP_aead_aes_128_gcm_siv();

  const size_t kNonceOffset = 0;
  const size_t kNonceSize = EVP_AEAD_nonce_length(algorithm);
  const size_t kTagOffset = kNonceOffset + kNonceSize;
  const size_t kTagSize = EVP_AEAD_max_tag_len(algorithm);
  const size_t kEncryptedDataOffset = kTagOffset + kTagSize;
  if (encrypted_data.size() < kEncryptedDataOffset) {
    return false;
  }
  const size_t kEncryptedDataSize =
      encrypted_data.size() - kEncryptedDataOffset;

  bssl::ScopedEVP_AEAD_CTX ctx;
  if (!EVP_AEAD_CTX_init_with_direction(
          ctx.get(), algorithm, reinterpret_cast<const uint8_t*>(key.data()),
          key.size(), EVP_AEAD_DEFAULT_TAG_LENGTH, evp_aead_open)) {
    return false;
  }

  const uint8_t* encrypted_data_as_uint8_ptr =
      reinterpret_cast<const uint8_t*>(encrypted_data.data());

  std::string result;
  result.resize(kEncryptedDataSize);

  size_t out_len;
  if (EVP_AEAD_CTX_open(
          ctx.get(), reinterpret_cast<uint8_t*>(&result[0]), &out_len,
          result.size(), &encrypted_data_as_uint8_ptr[kNonceOffset], kNonceSize,
          &encrypted_data_as_uint8_ptr[kEncryptedDataOffset],
          kEncryptedDataSize, &encrypted_data_as_uint8_ptr[kTagOffset],
          kTagSize) == 0) {
    return false;
  }

  result.resize(out_len);
  output->swap(result);
  return true;
}

}  // namespace encryption
