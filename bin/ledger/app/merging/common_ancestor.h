// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_APP_MERGING_COMMON_ANCESTOR_H_
#define PERIDOT_BIN_LEDGER_APP_MERGING_COMMON_ANCESTOR_H_

#include <functional>
#include <memory>

#include "lib/fxl/memory/ref_counted.h"
#include "lib/ledger/fidl/ledger.fidl.h"
#include "peridot/bin/ledger/storage/public/page_storage.h"

namespace ledger {

void FindCommonAncestor(
    storage::PageStorage* storage,
    std::unique_ptr<const storage::Commit> head1,
    std::unique_ptr<const storage::Commit> head2,
    std::function<void(Status, std::unique_ptr<const storage::Commit>)>
        callback);

}  // namespace ledger

#endif  // PERIDOT_BIN_LEDGER_APP_MERGING_COMMON_ANCESTOR_H_
