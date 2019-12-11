// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/encryption/primitives/kdf.h"

#include <openssl/digest.h>
#include <openssl/hkdf.h>

#include "src/ledger/lib/logging/logging.h"
#include "src/lib/fxl/logging.h"
#include "third_party/abseil-cpp/absl/strings/string_view.h"

namespace encryption {

std::string HMAC256KDF(absl::string_view data, size_t length) {
  std::string output;
  output.resize(length);
  int result =
      HKDF(reinterpret_cast<uint8_t*>(&output[0]), output.size(), EVP_sha256(),
           reinterpret_cast<const uint8_t*>(data.data()), data.size(), nullptr, 0u, nullptr, 0u);
  LEDGER_CHECK(result == 1);
  return output;
}

}  // namespace encryption
