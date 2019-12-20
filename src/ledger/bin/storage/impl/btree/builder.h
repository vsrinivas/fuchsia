// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_STORAGE_IMPL_BTREE_BUILDER_H_
#define SRC_LEDGER_BIN_STORAGE_IMPL_BTREE_BUILDER_H_

#include <lib/fit/function.h>

#include <memory>
#include <set>

#include "src/ledger/bin/storage/impl/btree/tree_node.h"
#include "src/ledger/bin/storage/public/iterator.h"
#include "src/ledger/bin/storage/public/page_storage.h"
#include "src/ledger/bin/storage/public/types.h"
#include "src/ledger/lib/coroutine/coroutine.h"

namespace storage {
namespace btree {

struct NodeLevelCalculator {
  // Returns the level in the tree where a node containing |key| must be
  // located. The leaves are located on level 0.
  uint8_t (*GetNodeLevel)(convert::ExtendedStringView key);
};

// Returns the default algorithm to compute the node level.
const NodeLevelCalculator* GetDefaultNodeLevelCalculator();

// Applies changes provided by |changes| to the B-Tree starting at |root_identifier|. |changes| must
// provide |EntryChange| objects sorted by their key. |new_root_identifier| will contain the object
// identifier of the new root and |new_identifiers| the list of object identifiers of all new
// non-inline nodes created after the changes (not their individual pieces), ie. it will contain the
// set of nodes of the new tree that were not present in the original tree. Insertions are turned
// into updates if the key exists, and ignored if they only change the entry id. Deletions only need
// to mention the key that is being deleted.
Status ApplyChanges(
    coroutine::CoroutineHandler* coroutine_handler, PageStorage* page_storage,
    LocatedObjectIdentifier root_identifier, std::vector<EntryChange> changes,
    ObjectIdentifier* new_root_identifier, std::set<ObjectIdentifier>* new_identifiers,
    const NodeLevelCalculator* node_level_calculator = GetDefaultNodeLevelCalculator());

// Applies changes provided by |changes| to the B-Tree starting at |root_identifier|. The |changes|
// are applied in order.  |new_root_identifier| will contain the object identifier of the new root
// and |new_identifiers| the list of object identifiers of all new nodes created after the changes
// (not their individual pieces), ie. it will contain the set of nodes of the new tree that were not
// present in the original tree.  Insertions are only valid if the key is not present, and deletions
// must match the content of the entry exactly.  Returns |INVALID_ARGUMENT| if the changes cannot be
// applied.
Status ApplyChangesFromCloud(
    coroutine::CoroutineHandler* coroutine_handler, PageStorage* page_storage,
    LocatedObjectIdentifier root_identifier, std::vector<EntryChange> changes,
    ObjectIdentifier* new_root_identifier, std::set<ObjectIdentifier>* new_identifiers,
    const NodeLevelCalculator* node_level_calculator = GetDefaultNodeLevelCalculator());

}  // namespace btree
}  // namespace storage

#endif  // SRC_LEDGER_BIN_STORAGE_IMPL_BTREE_BUILDER_H_
