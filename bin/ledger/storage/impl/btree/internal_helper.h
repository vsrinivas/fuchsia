// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_STORAGE_IMPL_BTREE_INTERNAL_HELPER_H_
#define PERIDOT_BIN_LEDGER_STORAGE_IMPL_BTREE_INTERNAL_HELPER_H_

#include <vector>

#include <lib/fxl/strings/string_view.h>

#include "peridot/bin/ledger/storage/public/types.h"

#define RETURN_ON_ERROR(expr)   \
  do {                          \
    Status status = (expr);     \
    if (status != Status::OK) { \
      return status;            \
    }                           \
  } while (0)

namespace storage {
namespace btree {

// Returns the index of |entries| that contains |key|, or the first entry that
// has a key greather than |key|. In the second case, the key, if present, will
// be found in the children at the returned index.
size_t GetEntryOrChildIndex(std::vector<Entry> entries, fxl::StringView key);

}  // namespace btree
}  // namespace storage

#endif  // PERIDOT_BIN_LEDGER_STORAGE_IMPL_BTREE_INTERNAL_HELPER_H_
