// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/glue/crypto/hash.h"

#include <openssl/sha.h>

namespace glue {

std::string SHA256Hash(const void* input, size_t input_lenght) {
  std::string result;
  result.resize(SHA256_DIGEST_LENGTH);
  SHA256_CTX sha256;
  SHA256_Init(&sha256);
  SHA256_Update(&sha256, input, input_lenght);
  SHA256_Final(reinterpret_cast<uint8_t*>(&result[0]), &sha256);
  return result;
}

}  // namespace glue
