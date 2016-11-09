// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_SRC_STORAGE_IMPL_BTREE_BTREE_BUILDER_H_
#define APPS_LEDGER_SRC_STORAGE_IMPL_BTREE_BTREE_BUILDER_H_

#include <unordered_set>

#include "apps/ledger/src/storage/impl/btree/tree_node.h"
#include "apps/ledger/src/storage/public/types.h"

namespace storage {

class BTreeBuilder {
 public:
  // Applies changes provided by |changes| to the BTree starting at |root_id|.
  // |changes| must provide |EntryChange| objects sorted by their key. The
  // callback will provide the status of the operation, the id of the new root
  // and the list of ids of all new nodes created after the changes.
  static void ApplyChanges(
      PageStorage* page_storage,
      ObjectIdView root_id,
      size_t node_size,
      std::unique_ptr<Iterator<const EntryChange>> changes,
      std::function<void(Status, ObjectId, std::unordered_set<ObjectId>&&)>
          callback);

 private:
  // Recursively applies the changes starting from the given |node|.
  static Status ApplyChanges(PageStorage* page_storage,
                             std::unique_ptr<const TreeNode> node,
                             size_t node_size,
                             const std::string& max_key,
                             std::unordered_set<ObjectId>* new_nodes,
                             Iterator<const EntryChange>* changes,
                             TreeNode::Mutation* parent_mutation,
                             ObjectId* new_id);

  // Recursively merge |left| and |right| nodes. |new_id| will contain the ID of
  // the new, merged node. Returns OK on success or the error code otherwise.
  static Status Merge(PageStorage* page_storage,
                      std::unique_ptr<const TreeNode> left,
                      std::unique_ptr<const TreeNode> right,
                      std::unordered_set<ObjectId>* new_nodes,
                      ObjectId* new_id);
};

}  // namespace storage

#endif  // APPS_LEDGER_SRC_STORAGE_IMPL_BTREE_BTREE_BUILDER_H_
