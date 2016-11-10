// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/storage/impl/btree/btree_utils.h"

namespace storage {
namespace btree {
namespace {

Status GetObjectsFromChild(const TreeNode& node,
                           int child_index,
                           std::set<ObjectId>* objects);

// Retrieves all objects in the subtree having |node| as root and adds them in
// |objects| set.
Status GetObjects(std::unique_ptr<const TreeNode> node,
                  std::set<ObjectId>* objects) {
  objects->insert(node->GetId());
  for (int i = 0; i < node->GetKeyCount(); ++i) {
    Status s = GetObjectsFromChild(*node, i, objects);
    if (s != Status::OK) {
      return s;
    }
    Entry e;
    s = node->GetEntry(i, &e);
    if (s != Status::OK) {
      return s;
    }
    objects->insert(e.object_id);
  }
  return GetObjectsFromChild(*node, node->GetKeyCount(), objects);
}

// Checks if the child is present in the given index and adds the objects in
// its subtree in the given set.
Status GetObjectsFromChild(const TreeNode& node,
                           int child_index,
                           std::set<ObjectId>* objects) {
  std::unique_ptr<const TreeNode> child;
  Status s = node.GetChild(child_index, &child);
  if (s == Status::NOT_FOUND) {
    return Status::OK;
  }
  if (s != Status::OK) {
    return s;
  }
  return GetObjects(std::move(child), objects);
}

}  // namespace

Status GetObjects(ObjectIdView root_id,
                  PageStorage* page_storage,
                  std::set<ObjectId>* objects) {
  FTL_DCHECK(!root_id.empty());

  std::set<ObjectId> result;
  std::unique_ptr<const TreeNode> root;
  Status s = TreeNode::FromId(page_storage, root_id, &root);
  if (s != Status::OK) {
    return s;
  }
  s = GetObjects(std::move(root), &result);
  if (s != Status::OK) {
    return s;
  }
  objects->swap(result);
  return Status::OK;
}

}  // namespace btree
}  // namespace storage
