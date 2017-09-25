// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_APP_DIFF_UTILS_H_
#define PERIDOT_BIN_LEDGER_APP_DIFF_UTILS_H_

#include <functional>

#include "lib/fxl/macros.h"
#include "lib/ledger/fidl/ledger.fidl.h"
#include "peridot/bin/ledger/storage/public/page_storage.h"

namespace ledger {
namespace diff_utils {

// Configure the pagination behavior of |ComputePageChange|.
enum class PaginationBehavior {
  NO_PAGINATION,
  BY_SIZE,
};

// Asynchronously creates a PageChange representing the diff of the two provided
// commits, starting from the given |min_key|. If |pagination_behavior| is
// |NO_PAGINATION|, it provides all results. If |pagination_behavior| is
// |BY_SIZE| it will provide a number of results that fit in a fidl message.
// The result, or an error, will be provided in |callback| status. The second
// argument of the callback is either a pair of the PageChangePtr, containing
// the diff result and the string representation of the next token if the
// result is paginated, or empty if there are no more results to return. Note
// that the PageChangePtr in the callback will be nullptr if the diff is empty.
void ComputePageChange(
    storage::PageStorage* storage,
    const storage::Commit& base,
    const storage::Commit& other,
    std::string prefix_key,
    std::string min_key,
    PaginationBehavior pagination_behavior,
    std::function<void(Status, std::pair<PageChangePtr, std::string>)>
        callback);

}  // namespace diff_utils
}  // namespace ledger

#endif  // PERIDOT_BIN_LEDGER_APP_DIFF_UTILS_H_
