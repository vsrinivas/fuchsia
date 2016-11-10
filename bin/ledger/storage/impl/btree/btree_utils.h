// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_SRC_STORAGE_IMPL_BTREE_BTREE_UTILS_H_
#define APPS_LEDGER_SRC_STORAGE_IMPL_BTREE_BTREE_UTILS_H_

#include "apps/ledger/src/storage/impl/btree/tree_node.h"
#include "apps/ledger/src/storage/public/types.h"

namespace storage {
namespace btree {

// Retrieves the ids of all objects in the BTree, i.e tree nodes and values of
// entries in the tree. After a successfull call, |objects| will be replaced
// with the result.
Status GetObjects(ObjectIdView root_id,
                  PageStorage* page_storage,
                  std::set<ObjectId>* objects);

}  // namespace btree
}  // namespace storage

#endif  // APPS_LEDGER_SRC_STORAGE_IMPL_BTREE_BTREE_UTILS_H_
