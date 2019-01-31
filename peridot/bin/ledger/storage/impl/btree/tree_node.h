// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_STORAGE_IMPL_BTREE_TREE_NODE_H_
#define PERIDOT_BIN_LEDGER_STORAGE_IMPL_BTREE_TREE_NODE_H_

#include <memory>
#include <vector>

#include <lib/fit/function.h>

#include "peridot/bin/ledger/storage/public/object.h"
#include "peridot/bin/ledger/storage/public/page_storage.h"
#include "peridot/bin/ledger/storage/public/types.h"
#include "peridot/lib/convert/convert.h"

namespace storage {
namespace btree {

// A node of the B-Tree holding the commit contents.
class TreeNode {
 public:
  ~TreeNode();

  // Creates a |TreeNode| object for an existing node and calls the given
  // |callback| with the returned status and node.
  static void FromIdentifier(
      PageStorage* page_storage, ObjectIdentifier identifier,
      fit::function<void(Status, std::unique_ptr<const TreeNode>)> callback);

  // Creates a |TreeNode| object with the given entries and children. |children|
  // is a map from the index of the child to the identfiier of the child. It
  // only contains non-empty children. It is expected that all child index are
  // between |0| and |size(entries)| (included). The |callback| will be called
  // with the success or error status and the id of the new node.
  static void FromEntries(
      PageStorage* page_storage, uint8_t level,
      const std::vector<Entry>& entries,
      const std::map<size_t, ObjectIdentifier>& children,
      fit::function<void(Status, ObjectIdentifier)> callback);

  // Creates an empty node, i.e. a TreeNode with no entries and an empty child
  // at index 0 and calls the callback with the result.
  static void Empty(PageStorage* page_storage,
                    fit::function<void(Status, ObjectIdentifier)> callback);

  // Returns the number of entries stored in this tree node.
  int GetKeyCount() const;

  // Finds the entry at position |index| and stores it in |entry|. |index| has
  // to be in [0, GetKeyCount() - 1].
  Status GetEntry(int index, Entry* entry) const;

  // Finds the child node at position |index| and calls the |callback| with the
  // result. |index| has to be in [0, GetKeyCount()]. If the child at the given
  // index is empty |NO_SUCH_CHILD| is returned and the value of |child| is not
  // updated.
  void GetChild(int index,
                fit::function<void(Status, std::unique_ptr<const TreeNode>)>
                    callback) const;

  // Searches for the given |key| in this node. If it is found, |OK| is
  // returned and index contains the index of the entry. If not, |NOT_FOUND|
  // is returned and index stores the index of the child node where the key
  // might be found.
  Status FindKeyOrChild(convert::ExtendedStringView key, int* index) const;

  const ObjectIdentifier& GetIdentifier() const;

  uint8_t level() const { return level_; }

  const std::vector<Entry>& entries() const { return entries_; }

  const std::map<size_t, ObjectIdentifier>& children_identifiers() const {
    return children_;
  }

 private:
  TreeNode(PageStorage* page_storage, ObjectIdentifier identifier,
           uint8_t level, std::vector<Entry> entries,
           std::map<size_t, ObjectIdentifier> children);

  // Creates a |TreeNode| object for an existing |object| and stores it in the
  // given |node|.
  static Status FromObject(PageStorage* page_storage,
                           ObjectIdentifier identifier,
                           std::unique_ptr<const Object> object,
                           std::unique_ptr<const TreeNode>* node);

  PageStorage* page_storage_;
  ObjectIdentifier identifier_;
  const uint8_t level_;
  const std::vector<Entry> entries_;
  const std::map<size_t, ObjectIdentifier> children_;
};

}  // namespace btree
}  // namespace storage

#endif  // PERIDOT_BIN_LEDGER_STORAGE_IMPL_BTREE_TREE_NODE_H_
