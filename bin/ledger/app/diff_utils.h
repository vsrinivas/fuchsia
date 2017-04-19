// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_SRC_APP_DIFF_UTILS_H_
#define APPS_LEDGER_SRC_APP_DIFF_UTILS_H_

#include <functional>

#include "apps/ledger/services/public/ledger.fidl.h"
#include "apps/ledger/src/storage/public/page_storage.h"
#include "lib/ftl/macros.h"

namespace ledger {
namespace diff_utils {
// Asynchronously creates a PageChange representing the diff of the two provided
// commits, starting from the given |min_key| and providing as many results as
// possible, given the |max_fidl_size| constraint. The result, or an error, will
// be provided in |callback| status. The second argument of the callback is a
// pair of the PageChangePtr, containing the diff result, and the string
// representation of the next token, if the result is paginated, or empty, if
// there are no more results to return.
void ComputePageChange(
    storage::PageStorage* storage,
    const storage::Commit& base,
    const storage::Commit& other,
    std::string min_key,
    size_t max_fidl_size,
    std::function<void(Status, std::pair<PageChangePtr, std::string>)>
        callback);

}  // namespace diff_utils
}  // namespace ledger

#endif  // APPS_LEDGER_SRC_APP_DIFF_UTILS_H_
