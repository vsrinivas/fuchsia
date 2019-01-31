// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/encryption/primitives/hash.h"

#include <lib/fxl/logging.h>
#include <openssl/sha.h>

namespace encryption {

std::string SHA256WithLengthHash(fxl::StringView data) {
  std::string result;
  result.resize(SHA256_DIGEST_LENGTH);
  SHA256_CTX sha256;
  SHA256_Init(&sha256);
  uint64_t size = data.size();
  SHA256_Update(&sha256, &size, sizeof(size));
  SHA256_Update(&sha256, data.data(), data.size());
  SHA256_Final(reinterpret_cast<uint8_t*>(&result[0]), &sha256);
  return result;
}

}  // namespace encryption
