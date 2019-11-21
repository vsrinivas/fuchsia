// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_STORAGE_IMPL_BTREE_INTERNAL_HELPER_H_
#define SRC_LEDGER_BIN_STORAGE_IMPL_BTREE_INTERNAL_HELPER_H_

#include <vector>

#include "src/ledger/bin/storage/public/types.h"
#include "third_party/abseil-cpp/absl/strings/string_view.h"

namespace storage {
namespace btree {

// Returns the index of |entries| that contains |key|, or the first entry that
// has a key greather than |key|. In the second case, the key, if present, will
// be found in the children at the returned index.
size_t GetEntryOrChildIndex(const std::vector<Entry>& entries, absl::string_view key);

}  // namespace btree
}  // namespace storage

#endif  // SRC_LEDGER_BIN_STORAGE_IMPL_BTREE_INTERNAL_HELPER_H_
