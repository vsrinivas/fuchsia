// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_LIB_LEDGER_CLIENT_TYPES_H_
#define PERIDOT_LIB_LEDGER_CLIENT_TYPES_H_

#include <algorithm>
#include <lib/fidl/cpp/array.h>
#include <lib/fidl/cpp/vector.h>

namespace modular {

// TODO(mesch): Not sure how useful that is. These types can lie.
typedef fidl::Array<uint8_t,16> LedgerPageId;
typedef fidl::Array<uint8_t,16> LedgerPageKey;
typedef fidl::VectorPtr<uint8_t> LedgerToken;

bool PageIdsEqual(const LedgerPageId& a, const LedgerPageId& b) {
  return std::equal(a.cbegin(), a.cend(), b.cbegin(), b.cend());
}

}  // namespace modular

#endif  // PERIDOT_LIB_LEDGER_CLIENT_TYPES_H_
