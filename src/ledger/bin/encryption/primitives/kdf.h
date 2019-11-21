// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_ENCRYPTION_PRIMITIVES_KDF_H_
#define SRC_LEDGER_BIN_ENCRYPTION_PRIMITIVES_KDF_H_

#include <string>

#include "third_party/abseil-cpp/absl/strings/string_view.h"

namespace encryption {
// Compute the key derivation function defined by RFC 5869 using HMAC-256 and
// the given |length|. The usual salt and info are omitted due to the fact that
// our scheme always passes unique data to the KDF.
std::string HMAC256KDF(absl::string_view data, size_t length);
}  // namespace encryption

#endif  // SRC_LEDGER_BIN_ENCRYPTION_PRIMITIVES_KDF_H_
