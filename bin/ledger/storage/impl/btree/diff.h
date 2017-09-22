// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_SRC_STORAGE_IMPL_BTREE_DIFF_H_
#define APPS_LEDGER_SRC_STORAGE_IMPL_BTREE_DIFF_H_

#include <functional>

#include "peridot/bin/ledger/coroutine/coroutine.h"
#include "peridot/bin/ledger/storage/impl/btree/tree_node.h"
#include "peridot/bin/ledger/storage/public/types.h"

namespace storage {
namespace btree {

// Iterates through the differences between two trees given their root ids
// |base_root_id| and |other_root_id| and calls |on_next| on found differences.
// Returning false from |on_next| will immediately stop the iteration. |on_done|
// is called once, upon successfull completion, i.e. when there are no more
// differences or iteration was interrupted, or if an error occurs.
void ForEachDiff(coroutine::CoroutineService* coroutine_service,
                 PageStorage* page_storage,
                 ObjectIdView base_root_id,
                 ObjectIdView other_root_id,
                 std::string min_key,
                 std::function<bool(EntryChange)> on_next,
                 std::function<void(Status)> on_done);

}  // namespace btree
}  // namespace storage

#endif  // APPS_LEDGER_SRC_STORAGE_IMPL_BTREE_DIFF_H_
