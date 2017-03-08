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
// commits. The result, or an error, will be provided in |callback|.
void ComputePageChange(storage::PageStorage* storage,
                       const storage::Commit& base,
                       const storage::Commit& other,
                       std::function<void(Status, PageChangePtr)> callback);

}  // namespace diff_utils
}  // namespace ledger

#endif  // APPS_LEDGER_SRC_APP_DIFF_UTILS_H_
