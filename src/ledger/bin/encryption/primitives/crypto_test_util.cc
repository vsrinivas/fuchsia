// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/encryption/primitives/crypto_test_util.h"

#include <cctype>

#include "src/ledger/lib/logging/logging.h"
#include "third_party/abseil-cpp/absl/strings/escaping.h"
#include "third_party/abseil-cpp/absl/strings/string_view.h"

namespace encryption {

std::string FromHex(absl::string_view data) {
  LEDGER_DCHECK(
      std::all_of(data.begin(), data.end(), [](unsigned char c) { return std::isxdigit(c); }));
  return absl::HexStringToBytes(data);
}

}  // namespace encryption
