// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_STORAGE_IMPL_BTREE_POSITION_H_
#define APPS_LEDGER_STORAGE_IMPL_BTREE_POSITION_H_

#include <memory>

#include "apps/ledger/storage/impl/btree/tree_node.h"

namespace storage {

// Position of the iterator within a B-Tree node.
struct Position {
  explicit Position(std::unique_ptr<const TreeNode> node,
                    int entry_index,
                    int child_index);
  ~Position();

  // Node being iterated over.
  std::unique_ptr<const TreeNode> node;
  // Position over the list of entries (the key/value pairs).
  int entry_index;
  // Position over the list of children of this node.
  int child_index;
};

}  // namespace storage

#endif  // APPS_LEDGER_STORAGE_IMPL_BTREE_POSITION_H_
