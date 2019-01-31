// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/encryption/primitives/crypto_test_util.h"

#include <lib/fxl/strings/string_number_conversions.h>

namespace encryption {

std::string FromHex(fxl::StringView data) {
  std::string result;
  result.reserve(data.size() / 2);
  while (!data.empty()) {
    result.push_back(
        fxl::StringToNumber<uint8_t>(data.substr(0, 2), fxl::Base::k16));
    data = data.substr(2);
  }
  return result;
}

}  // namespace encryption
