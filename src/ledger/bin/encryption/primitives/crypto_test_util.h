// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_ENCRYPTION_PRIMITIVES_CRYPTO_TEST_UTIL_H_
#define SRC_LEDGER_BIN_ENCRYPTION_PRIMITIVES_CRYPTO_TEST_UTIL_H_

#include <string>

#include "third_party/abseil-cpp/absl/strings/string_view.h"

namespace encryption {
std::string FromHex(absl::string_view data);
}

#endif  // SRC_LEDGER_BIN_ENCRYPTION_PRIMITIVES_CRYPTO_TEST_UTIL_H_
