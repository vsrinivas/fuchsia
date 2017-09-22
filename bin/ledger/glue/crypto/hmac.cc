// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/glue/crypto/hmac.h"

#include <openssl/digest.h>
#include <openssl/hmac.h>
#include <openssl/sha.h>

namespace glue {

std::string SHA256HMAC(fxl::StringView key, fxl::StringView data) {
  FXL_CHECK(key.size() >= SHA256_DIGEST_LENGTH);

  std::string result;
  result.resize(SHA256_DIGEST_LENGTH);
  unsigned int result_size;
  const uint8_t* out =
      HMAC(EVP_sha256(), key.data(), key.size(),
           reinterpret_cast<const uint8_t*>(data.data()), data.size(),
           reinterpret_cast<uint8_t*>(&result[0]), &result_size);
  FXL_CHECK(out);
  FXL_DCHECK(result_size == SHA256_DIGEST_LENGTH);
  result.resize(result_size);
  return result;
}

}  // namespace glue
