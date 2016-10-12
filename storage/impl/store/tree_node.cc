// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/storage/impl/store/tree_node.h"

#include <algorithm>

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
                   ObjectIdView id,
                   const std::vector<Entry>& entries,
                   const std::vector<ObjectId>& children)
    : store_(store),
      id_(id.ToString()),
      entries_(entries),
      children_(children) {}

TreeNode::~TreeNode() {}

Status TreeNode::FromId(ObjectStore* store,
                        ObjectIdView id,
                        std::unique_ptr<const TreeNode>* node) {
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
                       ObjectIdView left,
                       ObjectIdView right,
                       ObjectIdView merged_child_id,
                       ObjectId* merged_id) {
  std::unique_ptr<const TreeNode> leftNode;
  std::unique_ptr<const TreeNode> rightNode;
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
  children.push_back(merged_child_id.ToString());
  children.insert(children.end(), rightNode->children_.begin() + 1,
                  rightNode->children_.end());

  return FromEntries(store, entries, children, merged_id);
}

TreeNode::Mutation TreeNode::StartMutation() const {
  return TreeNode::Mutation(*this);
}

Status TreeNode::Split(int index,
                       ObjectIdView left_rightmost_child,
                       ObjectIdView right_leftmost_child,
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
  children.push_back(left_rightmost_child.ToString());
  // TODO(nellyv): replace random id with the hash over the stored bytes.
  ObjectId leftId = RandomId();
  Status s = FromEntries(store_, entries, children, &leftId);
  if (s != Status::OK) {
    return s;
  }

  entries.clear();
  children.clear();
  // Right node
  children.push_back(right_leftmost_child.ToString());
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

Status TreeNode::GetChild(int index,
                          std::unique_ptr<const TreeNode>* child) const {
  FTL_DCHECK(index >= 0 && index <= GetKeyCount());
  if (children_[index].empty()) {
    return Status::NOT_FOUND;
  }
  return store_->GetTreeNode(children_[index], child);
}

Status TreeNode::FindKeyOrChild(const std::string& key, int* index) const {
  auto it = std::lower_bound(entries_.begin(), entries_.end(), key,
                             [](const Entry& entry, const std::string& key) {
                               return entry.key < key;
                             });
  if (it == entries_.end()) {
    *index = entries_.size();
    return Status::NOT_FOUND;
  }
  *index = it - entries_.begin();
  if (it->key == key) {
    return Status::OK;
  }
  return Status::NOT_FOUND;
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

// TreeNode::Mutation
TreeNode::Mutation::Mutation(const TreeNode& node) : node_(node) {}

TreeNode::Mutation::~Mutation() {}

TreeNode::Mutation& TreeNode::Mutation::AddEntry(const Entry& entry,
                                                 ObjectIdView left_id,
                                                 ObjectIdView right_id) {
  FTL_DCHECK(!finished);
  FTL_DCHECK(entries_.empty() || entries_.back().key < entry.key);
  CopyUntil(entry.key);

  entries_.push_back(entry);
  if (children_.size() < entries_.size()) {
    children_.push_back(left_id.ToString());
  } else {
    // On two consecutive |AddEntry| calls or |RemoveEntry| and AddEntry
    // calls the last defined child must match the given |left_id|.
    FTL_DCHECK(children_.back() == left_id);
  }
  children_.push_back(right_id.ToString());

  return *this;
}

TreeNode::Mutation& TreeNode::Mutation::UpdateEntry(const Entry& entry) {
  FTL_DCHECK(!finished);
  FTL_DCHECK(entries_.empty() || entries_.back().key <= entry.key);
  CopyUntil(entry.key);

  entries_.push_back(entry);
  if (children_.size() < entries_.size()) {
    children_.push_back(node_.children_[node_index_]);
  }
  ++node_index_;

  return *this;
}

TreeNode::Mutation& TreeNode::Mutation::RemoveEntry(const std::string& key,
                                                    ObjectIdView child_id) {
  FTL_DCHECK(!finished);
  FTL_DCHECK(entries_.empty() || entries_.back().key < key);
  CopyUntil(key);

  FTL_DCHECK(node_.entries_[node_index_].key == key);
  if (children_.size() == entries_.size()) {
    children_.push_back(child_id.ToString());
  } else {
    // On two consecutive |RemoveEntry| calls the last defined child must
    // match the given |child_id|.
    FTL_DCHECK(children_.back() == child_id);
  }
  ++node_index_;

  return *this;
}

TreeNode::Mutation& TreeNode::Mutation::UpdateChildId(
    const std::string& key_after,
    ObjectIdView child_id) {
  FTL_DCHECK(!finished);
  FTL_DCHECK(entries_.empty() || entries_.back().key < key_after);
  CopyUntil(key_after);

  children_.push_back(child_id.ToString());
  return *this;
}

Status TreeNode::Mutation::Finish(ObjectId* new_id) {
  FTL_DCHECK(!finished);
  CopyUntil("");

  // If the last change was not an AddEntry, the right child of the last entry
  // is not yet added.
  if (children_.size() == entries_.size()) {
    FTL_DCHECK(node_index_ == node_.GetKeyCount());
    children_.push_back(node_.children_[node_index_]);
  }

  finished = true;
  return FromEntries(node_.store_, entries_, children_, new_id);
}

void TreeNode::Mutation::CopyUntil(std::string key) {
  while (node_index_ < node_.GetKeyCount() &&
         (key.empty() || node_.entries_[node_index_].key < key)) {
    entries_.push_back(node_.entries_[node_index_]);
    // If a previous change (AddEntry or RemoveEntry) updated the previous
    // child, ignore node_.children_[i].
    if (children_.size() < entries_.size()) {
      children_.push_back(node_.children_[node_index_]);
    }
    ++node_index_;
  }
}

}  // namespace storage
