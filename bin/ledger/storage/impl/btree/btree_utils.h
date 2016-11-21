// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_SRC_STORAGE_IMPL_BTREE_BTREE_UTILS_H_
#define APPS_LEDGER_SRC_STORAGE_IMPL_BTREE_BTREE_UTILS_H_

#include <unordered_set>

#include "apps/ledger/src/storage/impl/btree/tree_node.h"
#include "apps/ledger/src/storage/public/types.h"

namespace storage {
namespace btree {

// Applies changes provided by |changes| to the BTree starting at |root_id|.
// |changes| must provide |EntryChange| objects sorted by their key. The
// callback will provide the status of the operation, the id of the new root
// and the list of ids of all new nodes created after the changes.
void ApplyChanges(
    PageStorage* page_storage,
    ObjectIdView root_id,
    size_t node_size,
    std::unique_ptr<Iterator<const EntryChange>> changes,
    std::function<void(Status, ObjectId, std::unordered_set<ObjectId>)>
        callback);

// Retrieves the ids of all objects in the BTree, i.e tree nodes and values of
// entries in the tree. After a successfull call, |objects| will be replaced
// with the result.
Status GetObjects(ObjectIdView root_id,
                  PageStorage* page_storage,
                  std::set<ObjectId>* objects);

// Tries to download all tree nodes and values with EAGER priority that are not
// locally available from sync. To do this PageStorage::GetObject is called for
// all corresponding objects.
void GetObjectsFromSync(ObjectIdView root_id,
                        PageStorage* page_storage,
                        std::function<void(Status)> callback);

}  // namespace btree
}  // namespace storage

#endif  // APPS_LEDGER_SRC_STORAGE_IMPL_BTREE_BTREE_UTILS_H_
