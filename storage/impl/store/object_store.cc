// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/storage/impl/store/object_store.h"

#include "apps/ledger/storage/impl/store/tree_node.h"
#include "apps/ledger/storage/public/commit_contents.h"

namespace storage {

ObjectStore::ObjectStore() {}
ObjectStore::~ObjectStore() {}

Status ObjectStore::AddObject(std::unique_ptr<Object> object) {
  map_[object->GetId()] = std::move(object);
  return Status::OK;
}

Status ObjectStore::GetBlob(const ObjectId& id,
                            std::unique_ptr<const Blob>* blob) {
  return Status::NOT_IMPLEMENTED;
}

Status ObjectStore::GetTreeNode(const ObjectId& id,
                                std::unique_ptr<const TreeNode>* tree_node) {
  auto iterator = map_.find(id);
  if (iterator == map_.end()) {
    return Status::NOT_FOUND;
  }
  TreeNode* node = static_cast<TreeNode*>(iterator->second.get());
  tree_node->reset(new TreeNode(*node));
  return Status::OK;
}

}  // namespace storage
