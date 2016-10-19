// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/storage/impl/store/tree_node.h"

#include <algorithm>

#include "apps/ledger/convert/convert.h"
#include "apps/ledger/storage/impl/store/encoding.h"
#include "apps/ledger/storage/public/constants.h"
#include "lib/ftl/logging.h"

namespace storage {

TreeNode::TreeNode(ObjectStore* store,
                   std::string&& id,
                   std::vector<Entry>&& entries,
                   std::vector<ObjectId>&& children)
    : store_(store),
      id_(std::move(id)),
      entries_(entries),
      children_(children) {
  FTL_DCHECK(entries_.size() + 1 == children_.size());
}

TreeNode::~TreeNode() {}

Status TreeNode::FromId(ObjectStore* store,
                        ObjectIdView id,
                        std::unique_ptr<const TreeNode>* node) {
  std::unique_ptr<const Object> object;
  Status status = store->GetObject(id, &object);
  if (status != Status::OK) {
    return status;
  }
  ftl::StringView json;
  status = object->GetData(&json);
  if (status != Status::OK)
    return status;
  std::vector<Entry> entries;
  std::vector<ObjectId> children;
  if (!DecodeNode(json, &entries, &children)) {
    return Status::FORMAT_ERROR;
  }
  node->reset(new TreeNode(store, object->GetId(), std::move(entries),
                           std::move(children)));
  return Status::OK;
}

Status TreeNode::FromEntries(ObjectStore* store,
                             const std::vector<Entry>& entries,
                             const std::vector<ObjectId>& children,
                             ObjectId* node_id) {
  FTL_DCHECK(entries.size() + 1 == children.size());
  std::string encoding = storage::EncodeNode(entries, children);
  std::unique_ptr<const Object> object;
  Status s = store->AddObject(encoding, &object);
  if (s != Status::OK) {
    return s;
  }
  *node_id = object->GetId();
  return Status::OK;
}

Status TreeNode::Merge(ObjectStore* store,
                       std::unique_ptr<const TreeNode> left,
                       std::unique_ptr<const TreeNode> right,
                       ObjectIdView merged_child_id,
                       ObjectId* merged_id) {
  std::vector<Entry> entries;
  entries.insert(entries.end(), left->entries_.begin(), left->entries_.end());
  entries.insert(entries.end(), right->entries_.begin(), right->entries_.end());

  std::vector<ObjectId> children;
  // Skip the last child of left, the first of the right and add merged_child_id
  // instead.
  children.insert(children.end(), left->children_.begin(),
                  left->children_.end() - 1);
  children.push_back(merged_child_id.ToString());
  children.insert(children.end(), right->children_.begin() + 1,
                  right->children_.end());

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
  ObjectId left_id;
  Status s = FromEntries(store_, entries, children, &left_id);
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
  ObjectId right_id;
  s = FromEntries(store_, entries, children, &right_id);
  if (s != Status::OK) {
    // TODO(nellyv): If this fails, remove the left  object from the object
    // store.
    return s;
  }

  left->swap(left_id);
  right->swap(right_id);
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
  return FromId(store_, children_[index], child);
}

Status TreeNode::FindKeyOrChild(convert::ExtendedStringView key,
                                int* index) const {
  auto it =
      std::lower_bound(entries_.begin(), entries_.end(), key,
                       [](const Entry& entry, convert::ExtendedStringView key) {
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

// TreeNode::Mutation
TreeNode::Mutation::Mutation(const TreeNode& node) : node_(node) {}

TreeNode::Mutation::~Mutation() {}

TreeNode::Mutation& TreeNode::Mutation::AddEntry(const Entry& entry,
                                                 ObjectIdView left_id,
                                                 ObjectIdView right_id) {
  FTL_DCHECK(!finished) << "Mutation " << this << " already finished.";
  FTL_DCHECK(entries_.empty() || entries_.back().key < entry.key)
      << "Failed at entry.key " << entry.key << " entries_.size() "
      << entries_.size();
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

void TreeNode::Mutation::FinalizeEntriesChildren() {
  CopyUntil("");

  // If the last change was not an AddEntry, the right child of the last entry
  // is not yet added.
  if (children_.size() == entries_.size()) {
    FTL_DCHECK(node_index_ == node_.GetKeyCount());
    children_.push_back(node_.children_[node_index_]);
  }

  finished = true;
}

Status TreeNode::Mutation::Finish(ObjectId* new_id) {
  FTL_DCHECK(!finished);
  FinalizeEntriesChildren();
  return FromEntries(node_.store_, entries_, children_, new_id);
}

Status TreeNode::Mutation::Finish(size_t max_size,
                                  Mutation* parent_mutation,
                                  const std::string& max_key,
                                  ObjectId* new_root_id) {
  FinalizeEntriesChildren();
  // If we want N nodes, each with S entries, separated by 1 entry, then the
  // total number of entries E is E = N*S+(N-1), leading to N=(E+1)/(S+1). As
  // integer division rounds down, we remove one to the dividand and add 1 to
  // the result to get the rounded up number.
  size_t new_node_count = 1 + entries_.size() / (max_size + 1);
  if (new_node_count == 1) {
    ObjectId result_id;
    ObjectId new_id;
    Status s = FromEntries(node_.store_, entries_, children_, &new_id);
    if (s != Status::OK) {
      return s;
    }
    if (parent_mutation) {
      parent_mutation->UpdateChildId(max_key, new_id);
    } else {
      new_root_id->swap(new_id);
    }
    return Status::OK;
  }

  std::vector<Entry> new_entries;
  std::vector<ObjectId> new_children;

  size_t elements_per_node =
      1 + (entries_.size() - new_node_count) / new_node_count;
  for (size_t i = 0; i < new_node_count; ++i) {
    int element_count = std::min(elements_per_node, entries_.size());
    std::vector<Entry> entries;
    std::vector<ObjectId> children;

    // Select entries for the split node.
    entries.insert(entries.end(), &entries_[0], &entries_[element_count]);
    entries_.erase(entries_.begin(), entries_.begin() + (element_count));

    // Select children for the split node. There is one more than the number of
    // entries.
    children.insert(children.end(), &children_[0],
                    &children_[element_count + 1]);
    children_.erase(children_.begin(), children_.begin() + (element_count + 1));

    ObjectId new_id;
    FromEntries(node_.store_, entries, children, &new_id);
    new_children.push_back(new_id);

    if (entries_.size() != 0) {
      // Save the pivot that needs to be moved up one level in the tree.
      new_entries.push_back(*entries_.begin());
      entries_.erase(entries_.begin());
    }
  }

  // All entries and children must have been allocated.
  FTL_DCHECK(entries_.size() == 0) << "Entries left: " << entries_.size();
  FTL_DCHECK(children_.size() == 0) << "Entries left: " << entries_.size();

  if (parent_mutation) {
    // Move the pivots to the parent node.
    for (size_t i = 0; i < new_entries.size(); ++i) {
      parent_mutation->AddEntry(new_entries[i], new_children[i],
                                new_children[i + 1]);
    }
    return Status::OK;
  }

  // No parent node, create a new one.
  std::unique_ptr<const TreeNode> new_node;
  ObjectId tmp_node_id;
  FTL_DCHECK(TreeNode::FromEntries(node_.store_, std::vector<Entry>(),
                                   std::vector<ObjectId>{ObjectId()},
                                   &tmp_node_id) == Status::OK);
  FTL_DCHECK(TreeNode::FromId(node_.store_, tmp_node_id, &new_node) ==
             Status::OK);
  // new_entries could contain more than max_size elements, so we can't directly
  // create the root using FromEntries. We use a mutation instead.
  TreeNode::Mutation mutation = new_node->StartMutation();
  for (size_t i = 0; i < new_entries.size(); ++i) {
    mutation.AddEntry(new_entries[i], new_children[i], new_children[i + 1]);
  }
  return mutation.Finish(max_size, parent_mutation, max_key, new_root_id);
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
