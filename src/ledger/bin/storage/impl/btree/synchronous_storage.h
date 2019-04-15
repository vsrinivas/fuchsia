// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_STORAGE_IMPL_BTREE_SYNCHRONOUS_STORAGE_H_
#define SRC_LEDGER_BIN_STORAGE_IMPL_BTREE_SYNCHRONOUS_STORAGE_H_

#include <lib/callback/waiter.h>

#include <memory>
#include <vector>

#include "src/ledger/bin/storage/impl/btree/tree_node.h"
#include "src/ledger/bin/storage/public/page_storage.h"
#include "src/ledger/bin/storage/public/types.h"
#include "src/ledger/lib/coroutine/coroutine.h"

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

  Status TreeNodeFromIdentifier(ObjectIdentifier object_identifier,
                                std::unique_ptr<const TreeNode>* result);

  Status TreeNodesFromIdentifiers(
      std::vector<ObjectIdentifier> object_identifiers,
      std::vector<std::unique_ptr<const TreeNode>>* result);

  Status TreeNodeFromEntries(uint8_t level, const std::vector<Entry>& entries,
                             const std::map<size_t, ObjectIdentifier>& children,
                             ObjectIdentifier* result);

 private:
  PageStorage* page_storage_;
  coroutine::CoroutineHandler* handler_;

  FXL_DISALLOW_COPY_AND_ASSIGN(SynchronousStorage);
};

}  // namespace btree
}  // namespace storage

#endif  // SRC_LEDGER_BIN_STORAGE_IMPL_BTREE_SYNCHRONOUS_STORAGE_H_
