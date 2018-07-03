// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_ENCRYPTION_PRIMITIVES_HASH_H_
#define PERIDOT_BIN_LEDGER_ENCRYPTION_PRIMITIVES_HASH_H_

#include <memory>
#include <string>

#include <lib/fxl/macros.h>
#include <lib/fxl/strings/string_view.h>

namespace encryption {

constexpr static size_t kHashSize = 32;

std::string SHA256WithLengthHash(fxl::StringView data);

}  // namespace encryption

#endif  // PERIDOT_BIN_LEDGER_ENCRYPTION_PRIMITIVES_HASH_H_
