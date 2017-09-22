// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_SRC_STORAGE_IMPL_BTREE_TREE_NODE_H_
#define APPS_LEDGER_SRC_STORAGE_IMPL_BTREE_TREE_NODE_H_

#include <memory>
#include <unordered_set>
#include <vector>

#include "peridot/bin/ledger/convert/convert.h"
#include "peridot/bin/ledger/storage/public/object.h"
#include "peridot/bin/ledger/storage/public/page_storage.h"
#include "peridot/bin/ledger/storage/public/types.h"

namespace storage {
namespace btree {

// A node of the B-Tree holding the commit contents.
class TreeNode {
 public:
  ~TreeNode();

  // Creates a |TreeNode| object for an existing node and calls the given
  // |callback| with the returned status and node.
  static void FromId(
      PageStorage* page_storage,
      ObjectIdView id,
      std::function<void(Status, std::unique_ptr<const TreeNode>)> callback);

  // Creates a |TreeNode| object with the given entries and children. An empty
  // id in the children's vector indicates that there is no child in that
  // index. The |callback| will be called with the success or error status and
  // the id of the new node. It is expected that |children| = |entries| + 1.
  static void FromEntries(PageStorage* page_storage,
                          uint8_t level,
                          const std::vector<Entry>& entries,
                          const std::vector<ObjectId>& children,
                          std::function<void(Status, ObjectId)> callback);

  // Creates an empty node, i.e. a TreeNode with no entries and an empty child
  // at index 0 and calls the callback with the result.
  static void Empty(PageStorage* page_storage,
                    std::function<void(Status, ObjectId)> callback);

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
                std::function<void(Status, std::unique_ptr<const TreeNode>)>
                    callback) const;

  // Returns the id of the child node at position |index|. If the child at the
  // given index is empty, an empty string is returned. |index| has to be in [0,
  // GetKeyCount()].
  ObjectIdView GetChildId(int index) const;

  // Searches for the given |key| in this node. If it is found, |OK| is
  // returned and index contains the index of the entry. If not, |NOT_FOUND|
  // is returned and index stores the index of the child node where the key
  // might be found.
  Status FindKeyOrChild(convert::ExtendedStringView key, int* index) const;

  const ObjectId& GetId() const;

  uint8_t level() const { return level_; }

  const std::vector<Entry>& entries() const { return entries_; }

  const std::vector<ObjectId>& children_ids() const { return children_; }

 private:
  TreeNode(PageStorage* page_storage,
           std::string id,
           uint8_t level,
           std::vector<Entry> entries,
           std::vector<ObjectId> children);

  // Creates a |TreeNode| object for an existing |object| and stores it in the
  // given |node|.
  static Status FromObject(PageStorage* page_storage,
                           std::unique_ptr<const Object> object,
                           std::unique_ptr<const TreeNode>* node);

  PageStorage* page_storage_;
  ObjectId id_;
  const uint8_t level_;
  const std::vector<Entry> entries_;
  const std::vector<ObjectId> children_;
};

}  // namespace btree
}  // namespace storage

#endif  // APPS_LEDGER_SRC_STORAGE_IMPL_BTREE_TREE_NODE_H_
