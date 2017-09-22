// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/storage/impl/btree/internal_helper.h"

#include <algorithm>

namespace storage {
namespace btree {

// Returns the index of |entries| that contains |key|, or the first entry that
// has key greather than |key|. In the second case, the key, if present, will
// be found in the children at the returned index.
size_t GetEntryOrChildIndex(const std::vector<Entry> entries,
                            fxl::StringView key) {
  auto lower = std::lower_bound(
      entries.begin(), entries.end(), key,
      [](const Entry& entry, fxl::StringView key) { return entry.key < key; });
  FXL_DCHECK(lower == entries.end() || lower->key >= key);
  return lower - entries.begin();
}

}  // namespace btree
}  // namespace storage
