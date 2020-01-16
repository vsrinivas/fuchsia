// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MODULAR_LIB_LEDGER_CLIENT_TYPES_H_
#define SRC_MODULAR_LIB_LEDGER_CLIENT_TYPES_H_

#include <fuchsia/ledger/cpp/fidl.h>
#include <lib/fidl/cpp/vector.h>

#include <algorithm>

namespace modular {

using LedgerPageId = fuchsia::ledger::PageId;
using LedgerPageKey = ::fidl::VectorPtr<uint8_t>;
using LedgerToken = std::unique_ptr<fuchsia::ledger::Token>;

inline bool PageIdsEqual(const LedgerPageId& a, const LedgerPageId& b) { return a.id == b.id; }

}  // namespace modular

#endif  // SRC_MODULAR_LIB_LEDGER_CLIENT_TYPES_H_
