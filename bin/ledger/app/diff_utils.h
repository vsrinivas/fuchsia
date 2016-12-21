// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_SRC_APP_DIFF_UTILS_H_
#define APPS_LEDGER_SRC_APP_DIFF_UTILS_H_

#include <functional>

#include "apps/ledger/services/public/ledger.fidl.h"
#include "apps/ledger/src/storage/public/commit_contents.h"
#include "apps/ledger/src/storage/public/page_storage.h"
#include "lib/ftl/macros.h"

namespace ledger {
namespace diff_utils {
// Asynchronously creates a PageChange from the diff of two CommitContents.
void ComputePageChange(
    storage::PageStorage* storage,
    int64_t change_timestamp,
    std::unique_ptr<storage::CommitContents> base,
    std::unique_ptr<storage::CommitContents> other,
    std::function<void(storage::Status, PageChangePtr)> callback);

}  // namespace diff_utils
}  // namespace ledger

#endif  // APPS_LEDGER_SRC_APP_DIFF_UTILS_H_
