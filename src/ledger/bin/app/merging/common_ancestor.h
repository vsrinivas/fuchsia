// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_APP_MERGING_COMMON_ANCESTOR_H_
#define SRC_LEDGER_BIN_APP_MERGING_COMMON_ANCESTOR_H_

#include <lib/fit/function.h>

#include <functional>
#include <memory>

#include "src/ledger/bin/fidl/include/types.h"
#include "src/ledger/bin/storage/public/page_storage.h"
#include "src/ledger/lib/coroutine/coroutine.h"

namespace ledger {

// Records the result of comparing two commits.
enum class CommitComparison : int {
  // Each commit contains changes that are not present in the other commit.
  UNORDERED,
  // All changes present in the left commit are present in the right commit.
  LEFT_SUBSET_OF_RIGHT,
  // All changes present in the right commit are present in the left commit.
  RIGHT_SUBSET_OF_LEFT,
  // The two commits contain the same set of changes.
  EQUIVALENT
};

// Find the set of lowest common ancestors of |left| and |right|, and returns a
// status, a list of ancestors, and a comparison result. If |left| is a subset
// of |right|, |right| a subset of |left|, or |left| and |right| are equivalent,
// the list of ancestors is empty and the comparison result is set to the
// appropriate value. Otherwise, the comparison result is set to
// UNCOMPARABLE.
Status FindCommonAncestors(coroutine::CoroutineHandler* handler, storage::PageStorage* storage,
                           std::unique_ptr<const storage::Commit> left,
                           std::unique_ptr<const storage::Commit> right,
                           CommitComparison* comparison,
                           std::vector<std::unique_ptr<const storage::Commit>>* ancestors);

}  // namespace ledger

#endif  // SRC_LEDGER_BIN_APP_MERGING_COMMON_ANCESTOR_H_
