// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_ENCRYPTION_PRIMITIVES_CRYPTO_TEST_UTIL_H_
#define PERIDOT_BIN_LEDGER_ENCRYPTION_PRIMITIVES_CRYPTO_TEST_UTIL_H_

#include <string>

#include <lib/fxl/strings/string_view.h>

namespace encryption {
std::string FromHex(fxl::StringView data);
}

#endif  // PERIDOT_BIN_LEDGER_ENCRYPTION_PRIMITIVES_CRYPTO_TEST_UTIL_H_
