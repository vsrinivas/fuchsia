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

// An entry and the id of the tree node in which it is stored.
struct EntryAndNodeId {
  const Entry& entry;
  const ObjectId& node_id;
};

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
// entries in the tree. After a successfull call, |callback| will be called
// with the set of results.
void GetObjectIds(PageStorage* page_storage,
                  ObjectIdView root_id,
                  std::function<void(Status, std::set<ObjectId>)> callback);

// Tries to download all tree nodes and values with EAGER priority that are not
// locally available from sync. To do this PageStorage::GetObject is called for
// all corresponding objects.
void GetObjectsFromSync(ObjectIdView root_id,
                        PageStorage* page_storage,
                        std::function<void(Status)> callback);

// Iterates through the nodes of the tree with the given root and calls
// |on_next| on found entries with a key equal to or greater than |min_key|.
// The return value of |on_next| can be used to stop the iteration: returning
// false will interrupt the iteration in progress and no more |on_next| calls
// will be made. |on_done| is called once, upon successfull completion, i.e.
// when there are no more elements or iteration was interrupted, or if an error
// occurs.
void ForEachEntry(PageStorage* page_storage,
                  ObjectIdView root_id,
                  std::string min_key,
                  std::function<bool(EntryAndNodeId)> on_next,
                  std::function<void(Status)> on_done);

}  // namespace btree
}  // namespace storage

#endif  // APPS_LEDGER_SRC_STORAGE_IMPL_BTREE_BTREE_UTILS_H_
