// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/encryption/primitives/kdf.h"

#include <lib/fxl/logging.h>
#include <openssl/digest.h>
#include <openssl/hkdf.h>

namespace encryption {

std::string HMAC256KDF(fxl::StringView data, size_t length) {
  std::string output;
  output.resize(length);
  int result = HKDF(reinterpret_cast<uint8_t*>(&output[0]), output.size(),
                    EVP_sha256(), reinterpret_cast<const uint8_t*>(data.data()),
                    data.size(), nullptr, 0u, nullptr, 0u);
  FXL_CHECK(result == 1);
  return output;
}

}  // namespace encryption
