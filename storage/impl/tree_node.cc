// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/storage/impl/tree_node.h"

#include "apps/ledger/glue/crypto/rand.h"
#include "apps/ledger/storage/public/constants.h"
#include "lib/ftl/logging.h"

namespace storage {

namespace {

storage::ObjectId RandomId() {
  std::string result;
  result.resize(kObjectIdSize);
  glue::RandBytes(&result[0], kObjectIdSize);
  return result;
}

}  // namespace

TreeNode::TreeNode(const TreeNode& node)
    : store_(node.store_),
      id_(node.id_),
      entries_(node.entries_),
      children_(node.children_) {
  FTL_DCHECK(entries_.size() + 1 == children_.size());
}

TreeNode::TreeNode(ObjectStore* store,
                   const ObjectId& id,
                   const std::vector<Entry>& entries,
                   const std::vector<ObjectId>& children)
    : store_(store), id_(id), entries_(entries), children_(children) {}

TreeNode::~TreeNode() {}

Status TreeNode::FromId(ObjectStore* store,
                        const ObjectId& id,
                        std::unique_ptr<TreeNode>* node) {
  return store->GetTreeNode(id, node);
}

Status TreeNode::FromEntries(ObjectStore* store,
                             const std::vector<Entry>& entries,
                             const std::vector<ObjectId>& children,
                             ObjectId* node_id) {
  // TODO(nellyv): replace random id with the hash over the stored bytes.
  ObjectId id = RandomId();
  Status s = store->AddObject(
      std::unique_ptr<Object>(new TreeNode(store, id, entries, children)));
  if (s != Status::OK) {
    return s;
  }
  node_id->swap(id);
  return Status::OK;
}

Status TreeNode::Merge(ObjectStore* store,
                       const ObjectId& left,
                       const ObjectId& right,
                       const ObjectId& merged_child_id,
                       ObjectId* merged_id) {
  std::unique_ptr<TreeNode> leftNode;
  std::unique_ptr<TreeNode> rightNode;
  Status s = store->GetTreeNode(left, &leftNode);
  if (s != Status::OK) {
    return s;
  }
  s = store->GetTreeNode(right, &rightNode);
  if (s != Status::OK) {
    return s;
  }

  std::vector<Entry> entries;
  entries.insert(entries.end(), leftNode->entries_.begin(),
                 leftNode->entries_.end());
  entries.insert(entries.end(), rightNode->entries_.begin(),
                 rightNode->entries_.end());

  std::vector<ObjectId> children;
  // Skip the last child of left, the first of the right and add merged_child_id
  // instead.
  children.insert(children.end(), leftNode->children_.begin(),
                  leftNode->children_.end() - 1);
  children.push_back(merged_child_id);
  children.insert(children.end(), rightNode->children_.begin() + 1,
                  rightNode->children_.end());

  return FromEntries(store, entries, children, merged_id);
}

Status TreeNode::Copy(std::vector<NodeUpdate>& updates,
                      ObjectId* new_id) const {
  return Status::NOT_IMPLEMENTED;
}

Status TreeNode::Split(int index,
                       const ObjectId& left_rightmost_child,
                       const ObjectId& right_leftmost_child,
                       ObjectId* left,
                       ObjectId* right) const {
  FTL_DCHECK(index >= 0 && index < GetKeyCount());
  // Left node
  std::vector<Entry> entries;
  std::vector<ObjectId> children;
  for (int i = 0; i < index; ++i) {
    entries.push_back(entries_[i]);
    children.push_back(children_[i]);
  }
  children.push_back(left_rightmost_child);
  // TODO(nellyv): replace random id with the hash over the stored bytes.
  ObjectId leftId = RandomId();
  Status s = FromEntries(store_, entries, children, &leftId);
  if (s != Status::OK) {
    return s;
  }

  entries.clear();
  children.clear();
  // Right node
  children.push_back(right_leftmost_child);
  for (int i = index; i < GetKeyCount(); ++i) {
    entries.push_back(entries_[i]);
    children.push_back(children_[i + 1]);
  }
  // TODO(nellyv): replace random id with the hash over the stored bytes.
  ObjectId rightId = RandomId();
  s = FromEntries(store_, entries, children, &rightId);
  if (s != Status::OK) {
    // TODO(nellyv): If this fails, remove the left  object from the object
    // store.
    return s;
  }

  left->swap(leftId);
  right->swap(rightId);
  return Status::OK;
}

int TreeNode::GetKeyCount() const {
  return entries_.size();
}

Status TreeNode::GetEntry(int index, Entry* entry) const {
  FTL_DCHECK(index >= 0 && index < GetKeyCount());
  *entry = entries_[index];
  return Status::OK;
}

Status TreeNode::GetChild(int index, std::unique_ptr<TreeNode>* child) const {
  FTL_DCHECK(index >= 0 && index <= GetKeyCount());
  if (children_[index].empty()) {
    return Status::NOT_FOUND;
  }
  return store_->GetTreeNode(children_[index], child);
}

Status TreeNode::FindKeyOrChild(const std::string& key, int* index) const {
  return Status::NOT_IMPLEMENTED;
}

ObjectId TreeNode::GetId() const {
  return id_;
}

Status TreeNode::GetSize(int64_t* size) {
  return Status::NOT_IMPLEMENTED;
}

Status TreeNode::GetData(const uint8_t** data) {
  return Status::NOT_IMPLEMENTED;
}

}  // namespace storage
