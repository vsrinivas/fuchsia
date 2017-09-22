// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_SRC_STORAGE_IMPL_BTREE_SYNCHRONOUS_STORAGE_H_
#define APPS_LEDGER_SRC_STORAGE_IMPL_BTREE_SYNCHRONOUS_STORAGE_H_

#include <memory>
#include <vector>

#include "apps/ledger/src/callback/waiter.h"
#include "apps/ledger/src/coroutine/coroutine.h"
#include "apps/ledger/src/storage/impl/btree/tree_node.h"
#include "apps/ledger/src/storage/public/page_storage.h"
#include "apps/ledger/src/storage/public/types.h"

namespace storage {
namespace btree {

// Wrapper for TreeNode and PageStorage that uses coroutines to make
// asynchronous calls look like synchronous ones.
class SynchronousStorage {
 public:
  SynchronousStorage(PageStorage* page_storage,
                     coroutine::CoroutineHandler* handler);

  PageStorage* page_storage() { return page_storage_; }
  coroutine::CoroutineHandler* handler() { return handler_; }

  Status TreeNodeFromId(ObjectIdView object_id,
                        std::unique_ptr<const TreeNode>* result);

  Status TreeNodesFromIds(std::vector<ObjectIdView> object_ids,
                          std::vector<std::unique_ptr<const TreeNode>>* result);

  Status TreeNodeFromEntries(uint8_t level,
                             const std::vector<Entry>& entries,
                             const std::vector<ObjectId>& children,
                             ObjectId* result);

 private:
  PageStorage* page_storage_;
  coroutine::CoroutineHandler* handler_;

  FXL_DISALLOW_COPY_AND_ASSIGN(SynchronousStorage);
};

}  // namespace btree
}  // namespace storage

#endif  // APPS_LEDGER_SRC_STORAGE_IMPL_BTREE_SYNCHRONOUS_STORAGE_H_
