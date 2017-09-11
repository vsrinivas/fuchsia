// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_SRC_APP_MERGING_COMMON_ANCESTOR_H_
#define APPS_LEDGER_SRC_APP_MERGING_COMMON_ANCESTOR_H_

#include <functional>
#include <memory>

#include "apps/ledger/services/public/ledger.fidl.h"
#include "apps/ledger/src/storage/public/page_storage.h"
#include "lib/fxl/memory/ref_counted.h"
#include "lib/fxl/tasks/task_runner.h"

namespace ledger {

void FindCommonAncestor(
    fxl::RefPtr<fxl::TaskRunner> task_runner,
    storage::PageStorage* storage,
    std::unique_ptr<const storage::Commit> head1,
    std::unique_ptr<const storage::Commit> head2,
    std::function<void(Status, std::unique_ptr<const storage::Commit>)>
        callback);

}  // namespace ledger

#endif  // APPS_LEDGER_SRC_APP_MERGING_COMMON_ANCESTOR_H_
