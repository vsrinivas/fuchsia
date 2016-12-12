// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/storage/impl/btree/btree_utils.h"

#include "apps/ledger/src/callback/waiter.h"
#include "lib/ftl/functional/make_copyable.h"

namespace storage {
namespace btree {
namespace {
// Iterates through the nodes of the subtree with |node| as root and calls
// |on_next| on each entry found with a key equal to or greater than |min_key|.
void ForEachEntryIn(PageStorage* page_storage,
                    std::unique_ptr<const TreeNode> node,
                    std::string min_key,
                    std::function<bool(EntryAndNodeId)> on_next,
                    std::function<void(Status)> on_done) {
  auto waiter = callback::Waiter<Status, const Object>::Create(Status::OK);

  int start_index;
  Status key_found = node->FindKeyOrChild(min_key, &start_index);

  // If the key is found call on_next with the corresponding entry. Otherwise,
  // handle directly the next child, which is already pointed by start_index.
  if (key_found != Status::NOT_FOUND) {
    if (key_found != Status::OK) {
      on_done(key_found);
      return;
    }
    Entry entry;
    Status s = node->GetEntry(start_index, &entry);
    if (s != Status::OK) {
      on_done(s);
      return;
    }
    EntryAndNodeId next{entry, node->GetId()};
    if (!on_next(next)) {
      on_done(s);
      return;
    }
    ++start_index;
  }

  for (int i = start_index; i <= node->GetKeyCount(); ++i) {
    if (!node->GetChildId(i).empty()) {
      page_storage->GetObject(node->GetChildId(i), waiter->NewCallback());
    } else {
      waiter->NewCallback()(Status::OK, nullptr);
    }
  }
  waiter->Finalize(ftl::MakeCopyable([
    parent = std::move(node), start_index, min_key = std::move(min_key),
    page_storage, on_next = std::move(on_next), on_done = std::move(on_done)
  ](Status s, std::vector<std::unique_ptr<const Object>> objects) {
    if (s != Status::OK) {
      on_done(s);
      return;
    }
    callback::StatusWaiter<Status> children_waiter(Status::OK);
    for (size_t i = 0; i < objects.size(); ++i) {
      if (objects[i] != nullptr) {
        std::unique_ptr<const TreeNode> node;
        s = TreeNode::FromObject(page_storage, std::move(objects[i]), &node);
        if (s != Status::OK) {
          on_done(s);
          return;
        }
        ForEachEntryIn(page_storage, std::move(node), min_key, on_next,
                       children_waiter.NewCallback());
      }
      if (i == objects.size() - 1) {
        break;
      }
      Entry entry;
      s = parent->GetEntry(start_index + i, &entry);
      if (s != Status::OK) {
        on_done(s);
        return;
      }
      EntryAndNodeId next{entry, parent->GetId()};
      if (!on_next(next)) {
        on_done(Status::OK);
        return;
      }
    }
    children_waiter.Finalize(on_done);
  }));
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
          // We try to remove an entry that is not in the tree. This is
          // expected, as journals collate all operations on a key in a single
          // change: if one does a put then a delete on a key, then we will only
          // see here the delete operation.
          FTL_VLOG(1) << "Failed to delete key " << key << ": No such entry.";
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
    std::function<void(Status, ObjectId, std::unordered_set<ObjectId>)>
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

void GetObjectIds(PageStorage* page_storage,
                  ObjectIdView root_id,
                  std::function<void(Status, std::set<ObjectId>)> callback) {
  FTL_DCHECK(!root_id.empty());
  auto object_ids = std::make_unique<std::set<ObjectId>>();
  object_ids->insert(root_id.ToString());

  auto on_next =
      [ page_storage, object_ids = object_ids.get() ](EntryAndNodeId e) {
    object_ids->insert(e.entry.object_id);
    object_ids->insert(e.node_id);
    return true;
  };
  auto on_done = ftl::MakeCopyable([
    object_ids = std::move(object_ids), callback = std::move(callback)
  ](Status status) {
    if (status != Status::OK) {
      callback(status, std::set<ObjectId>());
      return;
    }
    callback(status, std::move(*object_ids));
  });
  ForEachEntry(page_storage, root_id, "", std::move(on_next),
               std::move(on_done));
}

void GetObjectsFromSync(ObjectIdView root_id,
                        PageStorage* page_storage,
                        std::function<void(Status)> callback) {
  ftl::RefPtr<callback::Waiter<Status, const Object>> waiter_ =
      callback::Waiter<Status, const Object>::Create(Status::OK);
  auto on_next = [page_storage, waiter_](EntryAndNodeId e) {
    if (e.entry.priority == KeyPriority::EAGER) {
      page_storage->GetObject(e.entry.object_id, waiter_->NewCallback());
    }
    return true;
  };
  auto on_done = [ callback = std::move(callback), waiter_ ](Status status) {
    if (status != Status::OK) {
      callback(status);
      return;
    }
    waiter_->Finalize([callback = std::move(callback)](
        Status s, std::vector<std::unique_ptr<const Object>> objects) {
      callback(s);
    });
  };
  ForEachEntry(page_storage, root_id, "", std::move(on_next),
               std::move(on_done));
}

void ForEachEntry(PageStorage* page_storage,
                  ObjectIdView root_id,
                  std::string min_key,
                  std::function<bool(EntryAndNodeId)> on_next,
                  std::function<void(Status)> on_done) {
  FTL_DCHECK(!root_id.empty());
  page_storage->GetObject(
      root_id, ftl::MakeCopyable([
        min_key = std::move(min_key), page_storage,
        on_next = std::move(on_next), on_done = std::move(on_done)
      ](Status status, std::unique_ptr<const Object> object) mutable {
        if (status != Status::OK) {
          on_done(status);
          return;
        }
        std::unique_ptr<const TreeNode> root;
        status = TreeNode::FromObject(page_storage, std::move(object), &root);
        if (status != Status::OK) {
          on_done(status);
          return;
        }
        ForEachEntryIn(page_storage, std::move(root), std::move(min_key),
                       std::move(on_next),
                       ftl::MakeCopyable([on_done = std::move(on_done)](
                           Status s) { on_done(s); }));
      }));
}

}  // namespace btree
}  // namespace storage
