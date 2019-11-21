// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_ENCRYPTION_PRIMITIVES_HASH_H_
#define SRC_LEDGER_BIN_ENCRYPTION_PRIMITIVES_HASH_H_

#include <memory>
#include <string>

#include "third_party/abseil-cpp/absl/strings/string_view.h"

namespace encryption {

inline constexpr size_t kHashSize = 32;

std::string SHA256WithLengthHash(absl::string_view data);

}  // namespace encryption

#endif  // SRC_LEDGER_BIN_ENCRYPTION_PRIMITIVES_HASH_H_
