// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_STORAGE_IMPL_TREE_NODE_H_
#define APPS_LEDGER_STORAGE_IMPL_TREE_NODE_H_

#include <memory>
#include <vector>

#include "apps/ledger/storage/impl/object_store.h"
#include "apps/ledger/storage/public/commit_contents.h"
#include "apps/ledger/storage/public/object.h"
#include "apps/ledger/storage/public/types.h"

namespace storage {

struct NodeUpdate {
  // TODO(nellyv): define the updates.
};

// A node of the B-Tree holding the commit contents.
class TreeNode : public Object {
 public:
  // TODO(nellyv): remove copy constructor when tree nodes are stored on disk.
  explicit TreeNode(const TreeNode& node);

  ~TreeNode() override;

  // Creates a |TreeNode| object for an existing node and stores it in the given
  // |node|.
  static Status FromId(ObjectStore* store,
                       const ObjectId& id,
                       std::unique_ptr<TreeNode>* node);

  // Creates a |TreeNode| object with the given entries. Contents of |children|
  // are optional and if a child is not present, an empty id should be given in
  // the corresponding index. The id of the new node is stored in |node_id|. It
  // is expected that |children| = |entries| + 1.
  static Status FromEntries(ObjectStore* store,
                            const std::vector<Entry>& entries,
                            const std::vector<ObjectId>& children,
                            ObjectId* node_id);

  // Creates a new tree node by merging |left| and |right|. The id of the new
  // node is stored in |merged_id|.
  static Status Merge(ObjectStore* store,
                      const ObjectId& left,
                      const ObjectId& right,
                      const ObjectId& merged_child_id,
                      ObjectId* merged_id);

  // Creates a new tree node by copying this one and applying the given
  // |updates|. The id of the new node is stored in |copy_id|.
  Status Copy(std::vector<NodeUpdate>& updates, ObjectId* new_id) const;

  // Creates two new tree nodes by splitting this one. The |left| one will store
  // entries in [0, index) and the |right| one those in [index, GetKeyCount()).
  // The rightmost child of |left| will be set to |left_rightmost_child| and the
  // leftmost child of |right| will be set to |right_leftmost_child|. Both
  // |left_rightmost_child| and |right_leftmost_child| can be empty, if there is
  // no child in the given position.
  Status Split(int index,
               const ObjectId& left_rightmost_child,
               const ObjectId& right_leftmost_child,
               ObjectId* left,
               ObjectId* right) const;

  // Returns the number of entries stored in this tree node.
  int GetKeyCount() const;

  // Finds the entry at position |index| and stores it in |entry|. |index| has
  // to be in [0, GetKeyCount() - 1].
  Status GetEntry(int index, Entry* entry) const;

  // Finds the child node at position |index| and stores it in |child|. |index|
  // has to be in [0, GetKeyCount()].
  Status GetChild(int index, std::unique_ptr<TreeNode>* child) const;

  // Searches for the given |key| in this node. If it is found, |OK| is
  // returned and index contains the index of the entry. If not, |NOT_FOUND|
  // is returned and index stores the index of the child node where the key
  // might be found.
  Status FindKeyOrChild(const std::string& key, int* index) const;

  // Object:
  ObjectId GetId() const override;
  Status GetSize(int64_t* size) override;
  Status GetData(const uint8_t** data) override;

 private:
  TreeNode(ObjectStore* store,
           const ObjectId& id,
           const std::vector<Entry>& entries,
           const std::vector<ObjectId>& children);

  ObjectStore* store_;
  const ObjectId id_;
  const std::vector<Entry> entries_;
  const std::vector<ObjectId> children_;
};

}  // namespace storage

#endif  // APPS_LEDGER_STORAGE_IMPL_TREE_NODE_H_
