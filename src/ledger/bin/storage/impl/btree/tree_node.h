// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_STORAGE_IMPL_BTREE_TREE_NODE_H_
#define SRC_LEDGER_BIN_STORAGE_IMPL_BTREE_TREE_NODE_H_

#include <lib/fit/function.h>

#include <memory>
#include <vector>

#include "src/ledger/bin/storage/public/object.h"
#include "src/ledger/bin/storage/public/page_storage.h"
#include "src/ledger/bin/storage/public/types.h"
#include "src/ledger/lib/convert/convert.h"

namespace storage {
namespace btree {

// The identifier for a node of the B-tree, with a location where it can be searched.
struct LocatedObjectIdentifier {
  ObjectIdentifier identifier;
  PageStorage::Location location;
};

// A node of the B-Tree holding the commit contents.
class TreeNode {
 public:
  ~TreeNode();

  // Creates a |TreeNode| object in |page_storage| for an existing node and
  // calls the given |callback| with the returned status and node.
  static void FromIdentifier(PageStorage* page_storage, LocatedObjectIdentifier identifier,
                             fit::function<void(Status, std::unique_ptr<const TreeNode>)> callback);

  // Creates a |TreeNode| object in |page_storage| with the given entries and
  // children. |children| is a map from the index of the child to the identifier
  // of the child. It only contains non-empty children. It is expected that all
  // child indexes are between |0| and |size(entries)| (included). The
  // |callback| will be called with the success or error status and the id of
  // the new node.
  static void FromEntries(PageStorage* page_storage, uint8_t level,
                          const std::vector<Entry>& entries,
                          const std::map<size_t, ObjectIdentifier>& children,
                          fit::function<void(Status, ObjectIdentifier)> callback);

  // Creates an empty node in |page_storage|, i.e. a TreeNode with no entries
  // and an empty child at index 0 and calls the callback with the result.
  static void Empty(PageStorage* page_storage,
                    fit::function<void(Status, ObjectIdentifier)> callback);

  // Initializes |node| with a |TreeNode| object for an existing |object|.
  static Status FromObject(const Object& object, std::unique_ptr<const TreeNode>* node);

  // Returns the number of entries stored in this tree node.
  int GetKeyCount() const;

  // Finds the entry at position |index| and stores it in |entry|. |index| has
  // to be in [0, GetKeyCount() - 1].
  Status GetEntry(int index, Entry* entry) const;

  // Adds to |references| the references from this node to its non-inlined
  // children and values.
  void AppendReferences(ObjectReferencesAndPriority* references) const;

  // Searches for the given |key| in this node. If it is found, |OK| is
  // returned and index contains the index of the entry. If not,
  // |INTERNAL_NOT_FOUND| is returned and index stores the index of the child
  // node where the key might be found.
  Status FindKeyOrChild(convert::ExtendedStringView key, int* index) const;

  const ObjectIdentifier& GetIdentifier() const;

  uint8_t level() const { return level_; }

  const std::vector<Entry>& entries() const { return entries_; }

  const std::map<size_t, ObjectIdentifier>& children_identifiers() const { return children_; }

 private:
  TreeNode(ObjectIdentifier identifier, uint8_t level, std::vector<Entry> entries,
           std::map<size_t, ObjectIdentifier> children);

  ObjectIdentifier identifier_;
  const uint8_t level_;
  const std::vector<Entry> entries_;
  const std::map<size_t, ObjectIdentifier> children_;
};

}  // namespace btree
}  // namespace storage

#endif  // SRC_LEDGER_BIN_STORAGE_IMPL_BTREE_TREE_NODE_H_
