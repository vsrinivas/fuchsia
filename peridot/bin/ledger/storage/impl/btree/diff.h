// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_STORAGE_IMPL_BTREE_DIFF_H_
#define PERIDOT_BIN_LEDGER_STORAGE_IMPL_BTREE_DIFF_H_

#include <functional>

#include <lib/fit/function.h>

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
                 ObjectIdentifier base_root_identifier,
                 ObjectIdentifier other_root_identifier, std::string min_key,
                 fit::function<bool(EntryChange)> on_next,
                 fit::function<void(Status)> on_done);

// Iterates through the differences between three trees given their root ids and
// calls |on_next| if any difference is found between any pair.
// Returning false from |on_next| will immediately stop the iteration. |on_done|
// is called once, upon successful completion, i.e. when there are no more
// differences or iteration was interrupted, or if an error occurs.
void ForEachThreeWayDiff(coroutine::CoroutineService* coroutine_service,
                         PageStorage* page_storage,
                         ObjectIdentifier base_root_identifier,
                         ObjectIdentifier left_root_identifier,
                         ObjectIdentifier right_root_identifier,
                         std::string min_key,
                         fit::function<bool(ThreeWayChange)> on_next,
                         fit::function<void(Status)> on_done);

}  // namespace btree
}  // namespace storage

#endif  // PERIDOT_BIN_LEDGER_STORAGE_IMPL_BTREE_DIFF_H_
