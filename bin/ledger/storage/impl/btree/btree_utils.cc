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
  if (s == Status::NO_SUCH_CHILD) {
    return Status::OK;
  }
  if (s != Status::OK) {
    return s;
  }
  return GetObjects(std::move(child), objects);
}

// Recursively merge |left| and |right| nodes. |new_id| will contain the ID of
// the new, merged node. Returns OK on success or the error code otherwise.
Status Merge(PageStorage* page_storage,
             std::unique_ptr<const TreeNode> left,
             std::unique_ptr<const TreeNode> right,
             std::unordered_set<ObjectId>* new_nodes,
             ObjectId* new_id) {
  if (left == nullptr) {
    *new_id = right == nullptr ? "" : right->GetId();
    return Status::OK;
  }
  if (right == nullptr) {
    *new_id = left->GetId();
    return Status::OK;
  }

  // Merge the children before merging left and right.
  ObjectId child_id;
  std::unique_ptr<const TreeNode> left_rightmost_child;
  std::unique_ptr<const TreeNode> right_leftmost_child;
  Status left_status =
      left->GetChild(left->GetKeyCount(), &left_rightmost_child);
  Status right_status = right->GetChild(0, &left_rightmost_child);
  if (left_status != Status::OK && left_status != Status::NO_SUCH_CHILD) {
    return left_status;
  }
  if (right_status != Status::OK && right_status != Status::NO_SUCH_CHILD) {
    return right_status;
  }
  Status child_result =
      Merge(page_storage, std::move(left_rightmost_child),
            std::move(right_leftmost_child), new_nodes, &child_id);
  if (child_result != Status::OK) {
    return child_result;
  }

  auto it = new_nodes->find(left->GetId());
  if (it != new_nodes->end()) {
    new_nodes->erase(it);
  }
  it = new_nodes->find(right->GetId());
  if (it != new_nodes->end()) {
    new_nodes->erase(it);
  }

  Status s = TreeNode::Merge(page_storage, std::move(left), std::move(right),
                             child_id, new_id);
  if (s != Status::OK) {
    return s;
  }
  new_nodes->insert(*new_id);
  return Status::OK;
}

// Recursively applies the changes starting from the given |node|.
Status ApplyChanges(PageStorage* page_storage,
                    std::unique_ptr<const TreeNode> node,
                    size_t node_size,
                    const std::string& max_key,
                    std::unordered_set<ObjectId>* new_nodes,
                    Iterator<const EntryChange>* changes,
                    TreeNode::Mutation* parent_mutation,
                    ObjectId* new_id) {
  TreeNode::Mutation mutation = node->StartMutation();
  std::string key;

  while (changes->Valid() &&
         ((key = (*changes)->entry.key) < max_key || max_key.empty())) {
    int index;
    Status s = node->FindKeyOrChild(key, &index);
    if (s == Status::OK) {
      // The key was found in this node.
      if ((*changes)->deleted) {
        // Remove the entry after merging the children.
        ObjectId child_id;
        std::unique_ptr<const TreeNode> left;
        std::unique_ptr<const TreeNode> right;
        Status left_status = node->GetChild(index, &left);
        Status right_status = node->GetChild(index + 1, &right);
        if (left_status != Status::OK && left_status != Status::NO_SUCH_CHILD) {
          return left_status;
        }
        if (right_status != Status::OK &&
            right_status != Status::NO_SUCH_CHILD) {
          return right_status;
        }
        Status merge_status = Merge(page_storage, std::move(left),
                                    std::move(right), new_nodes, &child_id);
        if (merge_status != Status::OK) {
          return merge_status;
        }
        mutation.RemoveEntry(key, child_id);
      } else {
        // Update the entry's value.
        mutation.UpdateEntry((*changes)->entry);
      }
    } else if (s == Status::NOT_FOUND) {
      // The key was not found in this node.
      std::unique_ptr<const TreeNode> child;
      Status child_status = node->GetChild(index, &child);
      if (child_status == Status::OK) {
        // Recursively search for the key in the child and then update the child
        // id in this node in the corresponding index.
        std::string next_key;
        if (index == node->GetKeyCount()) {
          next_key = "";
        } else {
          Entry entry;
          node->GetEntry(index, &entry);
          next_key = entry.key;
        }
        ObjectId new_child_id;
        Status status =
            ApplyChanges(page_storage, std::move(child), node_size, next_key,
                         new_nodes, changes, &mutation, &new_child_id);
        if (status != Status::OK) {
          return status;
        }
        // The change iterator already advanced inside the nested ApplyChanges,
        // so we skip advancing it here.
        continue;
      } else if (child_status == Status::NO_SUCH_CHILD) {
        if ((*changes)->deleted) {
          // We try to remove an entry that is not in the tree.
          FTL_LOG(INFO) << "Failed to delete key " << key << ": No such entry.";
        } else {
          // Add the entry here. Since there is no child, both the new left and
          // right children are empty.
          mutation.AddEntry((*changes)->entry, "", "");
        }
      } else {
        // GetChild returned an error.
        return child_status;
      }
    } else {
      // FindKeyOrChild returned an error.
      return s;
    }
    changes->Next();
  }
  FTL_DCHECK(parent_mutation || !changes->Valid());

  return mutation.Finish(node_size, parent_mutation, max_key, new_nodes,
                         new_id);
}

}  // namespace

void ApplyChanges(
    PageStorage* page_storage,
    ObjectIdView root_id,
    size_t node_size,
    std::unique_ptr<Iterator<const EntryChange>> changes,
    std::function<void(Status, ObjectId, std::unordered_set<ObjectId>&&)>
        callback) {
  std::unordered_set<ObjectId> new_nodes;
  std::unique_ptr<const TreeNode> root;
  if (root_id.empty()) {
    ObjectId tmp_root_id;
    Status status =
        TreeNode::FromEntries(page_storage, std::vector<Entry>(),
                              std::vector<ObjectId>{ObjectId()}, &tmp_root_id);
    if (status != Status::OK) {
      callback(status, ObjectId(), std::move(new_nodes));
      return;
    }
    status = TreeNode::FromId(page_storage, tmp_root_id, &root);
    if (status != Status::OK) {
      callback(status, ObjectId(), std::move(new_nodes));
      return;
    }
  } else {
    Status status = TreeNode::FromId(page_storage, root_id, &root);
    if (status != Status::OK) {
      callback(status, ObjectId(), std::move(new_nodes));
      return;
    }
  }

  ObjectId new_id;
  Status status = ApplyChanges(page_storage, std::move(root), node_size, "",
                               &new_nodes, changes.get(), nullptr, &new_id);
  callback(status, new_id, std::move(new_nodes));
}

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
