// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_STORAGE_IMPL_BTREE_DIFF_H_
#define SRC_LEDGER_BIN_STORAGE_IMPL_BTREE_DIFF_H_

#include <lib/fit/function.h>

#include <functional>

#include "src/ledger/bin/storage/impl/btree/tree_node.h"
#include "src/ledger/bin/storage/public/types.h"
#include "src/ledger/lib/coroutine/coroutine.h"

namespace storage {
namespace btree {

// Iterates through the differences between two trees given their root ids |base_root_id| and
// |other_root_id| and calls |on_next| on found differences. Returning false from |on_next| will
// immediately stop the iteration. |on_done| is called once, upon successfull completion, i.e. when
// there are no more differences or iteration was interrupted, or if an error occurs.
void ForEachDiff(coroutine::CoroutineService* coroutine_service, PageStorage* page_storage,
                 LocatedObjectIdentifier base_root_identifier,
                 LocatedObjectIdentifier other_root_identifier, std::string min_key,
                 fit::function<bool(EntryChange)> on_next, fit::function<void(Status)> on_done);

// Similarly to |ForEachDiff|, iterates through the differences between two trees given their root
// ids and calls |on_next| on found differences. For each difference found, returns the TwoWayChange
// entry, allowing to identify both the previous and updated states.
void ForEachTwoWayDiff(coroutine::CoroutineService* coroutine_service, PageStorage* page_storage,
                       LocatedObjectIdentifier base_root_identifier,
                       LocatedObjectIdentifier other_root_identifier, std::string min_key,
                       fit::function<bool(TwoWayChange)> on_next,
                       fit::function<void(Status)> on_done);

// Iterates through the differences between three trees given their root ids and calls |on_next| if
// any difference is found between any pair. Returning false from |on_next| will immediately stop
// the iteration. |on_done| is called once, upon successful completion, i.e. when there are no more
// differences or iteration was interrupted, or if an error occurs.
void ForEachThreeWayDiff(coroutine::CoroutineService* coroutine_service, PageStorage* page_storage,
                         LocatedObjectIdentifier base_root_identifier,
                         LocatedObjectIdentifier left_root_identifier,
                         LocatedObjectIdentifier right_root_identifier, std::string min_key,
                         fit::function<bool(ThreeWayChange)> on_next,
                         fit::function<void(Status)> on_done);

}  // namespace btree
}  // namespace storage

#endif  // SRC_LEDGER_BIN_STORAGE_IMPL_BTREE_DIFF_H_
