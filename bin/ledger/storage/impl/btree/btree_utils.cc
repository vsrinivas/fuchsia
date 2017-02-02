// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/storage/impl/btree/btree_utils.h"

#include "apps/ledger/src/callback/asynchronous_callback.h"
#include "apps/ledger/src/callback/waiter.h"
#include "lib/ftl/functional/closure.h"
#include "lib/ftl/functional/make_copyable.h"

namespace storage {
namespace btree {
namespace {

// Helper functions for btree::ForEach.
void ForEachEntryInSubtree(PageStorage* page_storage,
                           std::unique_ptr<const TreeNode> node,
                           std::string min_key,
                           std::function<bool(EntryAndNodeId)> on_next,
                           std::function<void(Status, bool)> on_done);

// If |child_id| is not empty, calls |on_done| with the TreeNode corresponding
// to the id. Otherwise, calls |on_done| with Status::NO_SUCH_CHILD and nullptr.
void FindChild(
    PageStorage* page_storage,
    ObjectIdView child_id,
    std::function<void(Status, std::unique_ptr<const TreeNode>)> on_done) {
  if (child_id.empty()) {
    on_done(Status::NO_SUCH_CHILD, nullptr);
    return;
  }
  TreeNode::FromId(page_storage, child_id, std::move(on_done));
}

// Recursively iterates throught the child nodes and entries of |parent|
// starting at |index|. |on_done| is called with the return status and a bool
// indicating whether the iteration was interrupted by |on_next|.
void ForEachEntryInChildIndex(PageStorage* page_storage,
                              std::unique_ptr<const TreeNode> parent,
                              int index,
                              std::string min_key,
                              std::function<bool(EntryAndNodeId)> on_next,
                              std::function<void(Status, bool)> on_done) {
  if (index == parent->GetKeyCount() + 1) {
    on_done(Status::OK, false);
    return;
  }
  // First, find the child at index.
  FindChild(page_storage, parent->GetChildId(index), ftl::MakeCopyable([
              page_storage, parent = std::move(parent), index,
              min_key = std::move(min_key), on_next = std::move(on_next),
              on_done = std::move(on_done)
            ](Status s, std::unique_ptr<const TreeNode> child) mutable {
              if (s != Status::OK && s != Status::NO_SUCH_CHILD) {
                on_done(s, false);
                return;
              }
              if (child == nullptr) {
                // If the child was not found in the search branch, no need to
                // search again.
                min_key = "";
              }
              // Then, finish iterating through the subtree of that child.
              ForEachEntryInSubtree(
                  page_storage, std::move(child), min_key, on_next,
                  ftl::MakeCopyable([
                    page_storage, parent = std::move(parent), index,
                    min_key = std::move(min_key), on_next = std::move(on_next),
                    on_done = std::move(on_done)
                  ](Status s, bool interrupted) mutable {
                    if (s != Status::OK || interrupted) {
                      on_done(s, interrupted);
                      return;
                    }
                    // Then, add the entry right after the child.
                    if (index != parent->GetKeyCount()) {
                      Entry entry;
                      FTL_CHECK(parent->GetEntry(index, &entry) == Status::OK);
                      EntryAndNodeId next{entry, parent->GetId()};
                      if (!on_next(next)) {
                        on_done(Status::OK, true);
                        return;
                      }
                    }
                    // Finally, continue the recursion at index + 1.
                    ForEachEntryInChildIndex(page_storage, std::move(parent),
                                             index + 1, std::move(min_key),
                                             std::move(on_next),
                                             std::move(on_done));
                  }));
            }));
}

// Performs an in-order traversal of the subtree having |node| as root and calls
// |on_next| on each entry found with a key equal to or greater than |min_key|.
// |on_done| is called with the return status and a bool indicating whether the
// iteration was interrupted by |on_next|.
void ForEachEntryInSubtree(PageStorage* page_storage,
                           std::unique_ptr<const TreeNode> node,
                           std::string min_key,
                           std::function<bool(EntryAndNodeId)> on_next,
                           std::function<void(Status, bool)> on_done) {
  if (node == nullptr) {
    on_done(Status::OK, false);
    return;
  }
  // Supposing that min_key = "35":
  //  [10, 30, 40, 70]                [10, 35, 40, 70]
  //         /    \                      /    \
  //   [32, 35]  [49, 50]          [22, 34]  [38, 39]
  // In the left tree's root node, "35" is not found and start_index will be 2,
  // i.e. continue search in child node at index 2.
  // In the right tree's root node, "35" is found and start_index will be 1,
  // i.e. call |on_next| for entry at index 1 ("35") and continue in child node
  // at 2.
  int start_index;
  Status key_found = node->FindKeyOrChild(min_key, &start_index);
  // If the key is found call on_next with the corresponding entry. Otherwise,
  // handle directly the next child, which is already pointed by start_index.
  if (key_found != Status::NOT_FOUND) {
    if (key_found != Status::OK) {
      on_done(key_found, false);
      return;
    }

    Entry entry;
    FTL_CHECK(node->GetEntry(start_index, &entry) == Status::OK);
    EntryAndNodeId next{entry, node->GetId()};
    if (!on_next(next)) {
      on_done(Status::OK, true);
      return;
    }
    // The child is found, no need to search again.
    min_key = "";
    ++start_index;
  }

  ForEachEntryInChildIndex(page_storage, std::move(node), start_index,
                           std::move(min_key), std::move(on_next),
                           std::move(on_done));
}

void RemoveNodeId(const ObjectId& id, std::unordered_set<ObjectId>* nodes) {
  auto it = nodes->find(id);
  if (it != nodes->end()) {
    nodes->erase(it);
  }
}

// Helper functions for btree::ApplyChanges.

void ApplyChangesIn(
    PageStorage* page_storage,
    Iterator<const EntryChange>* changes,
    std::unique_ptr<const TreeNode> node,
    bool is_root,
    std::string max_key,
    size_t node_size,
    std::unordered_set<ObjectId>* new_nodes,
    std::function<void(Status,
                       ObjectId,
                       std::unique_ptr<TreeNode::Mutation::Updater>)> on_done);

// Returns the child node at the given index or nullptr if the child is empty.
// |callback| will be called with an |OK| status on success, including the case
// of an empty child, or the error status on failure.
void GetChild(
    const TreeNode& node,
    int index,
    std::function<void(Status, std::unique_ptr<const TreeNode>)> callback) {
  if (node.GetChildId(index).empty()) {
    callback(Status::OK, nullptr);
  } else {
    node.GetChild(index, std::move(callback));
  }
}

// Recursively merge |left| and |right| nodes. |new_id| will contain the ID of
// the new, merged node. Returns OK on success or the error code otherwise.
void Merge(PageStorage* page_storage,
           std::unique_ptr<const TreeNode> left,
           std::unique_ptr<const TreeNode> right,
           std::unordered_set<ObjectId>* new_nodes,
           std::function<void(Status, ObjectId)> on_done) {
  if (left == nullptr) {
    if (right == nullptr) {
      on_done(Status::OK, "");
      return;
    }
    on_done(Status::OK, right->GetId());
    return;
  }
  if (right == nullptr) {
    on_done(Status::OK, left->GetId());
    return;
  }

  auto waiter =
      callback::Waiter<Status, std::unique_ptr<const TreeNode>>::Create(
          Status::OK);
  // The rightmost child of left.
  GetChild(*left, left->GetKeyCount(), waiter->NewCallback());
  // The leftmost child of right.
  GetChild(*right, 0, waiter->NewCallback());

  waiter->Finalize(ftl::MakeCopyable([
    page_storage, new_nodes, left = std::move(left), right = std::move(right),
    on_done = std::move(on_done)
  ](Status s, std::vector<std::unique_ptr<const TreeNode>> children) mutable {
    // Merge the children before merging left and right.
    Merge(page_storage, std::move(children[0]), std::move(children[1]),
          new_nodes, ftl::MakeCopyable([
            page_storage, left = std::move(left), right = std::move(right),
            new_nodes, on_done = std::move(on_done)
          ](Status s, ObjectId child_id) mutable {
            if (s != Status::OK) {
              on_done(s, "");
              return;
            }
            RemoveNodeId(left->GetId(), new_nodes);
            RemoveNodeId(right->GetId(), new_nodes);

            TreeNode::Merge(page_storage, std::move(left), std::move(right),
                            child_id,
                            [ new_nodes, on_done = std::move(on_done) ](
                                Status s, ObjectId merged_id) {
                              if (s == Status::OK) {
                                new_nodes->insert(merged_id);
                              }
                              on_done(s, merged_id);
                            });
          }));
  }));
}

// Applies the change to the given node. If the change is a deletion, it also
// triggers the merging of the corresponding children. |new_nodes| will be
// updated by adding all newly created nodes and removing the previous ones. The
// pointed object of |new_nodes| should outlive the call to the |on_done|
// callback.
void ApplyChangeOnNode(
    PageStorage* page_storage,
    const EntryChange* change,
    const TreeNode* node,
    int change_index,
    std::unordered_set<ObjectId>* new_nodes,
    std::function<void(Status, std::unique_ptr<TreeNode::Mutation::Updater>)>
        on_done) {
  if (!change->deleted) {
    // Update the entry's value.
    on_done(
        Status::OK,
        std::make_unique<TreeNode::Mutation::Updater>([entry = change->entry](
            TreeNode::Mutation * mutation) { mutation->UpdateEntry(entry); }));
    return;
  }

  auto waiter =
      callback::Waiter<Status, std::unique_ptr<const TreeNode>>::Create(
          Status::OK);
  // Get the left and right children.
  GetChild(*node, change_index, waiter->NewCallback());
  GetChild(*node, change_index + 1, waiter->NewCallback());

  waiter->Finalize(ftl::MakeCopyable([
    page_storage, new_nodes, key = change->entry.key,
    on_done = std::move(on_done)
  ](Status s, std::vector<std::unique_ptr<const TreeNode>> children) mutable {
    if (s != Status::OK) {
      on_done(s, nullptr);
      return;
    }
    // Remove the entry after merging the children.
    Merge(
        page_storage, std::move(children[0]), std::move(children[1]), new_nodes,
        [ key = std::move(key), on_done = std::move(on_done) ](
            Status s, ObjectId child_id) mutable {
          if (s != Status::OK) {
            on_done(s, nullptr);
            return;
          }
          on_done(Status::OK, std::make_unique<TreeNode::Mutation::Updater>([
                    key = std::move(key), child_id = std::move(child_id)
                  ](TreeNode::Mutation * mutation) {
                    mutation->RemoveEntry(std::move(key), std::move(child_id));
                  }));
        });
  }));
}

// Retrieves the child node in the given |child_index| and, if present,
// recursively calls ApplyChangesIn to apply all necessary changes to the
// subtree with that child as root. When |on_done| is called, the |changes|
// iterator will already be advanced to the first change that has not been
// applied, or to the end of the iterator if there is no such element.
void ApplyChangeOnKeyNotFound(
    PageStorage* page_storage,
    Iterator<const EntryChange>* changes,
    const TreeNode* node,
    int child_index,
    int node_size,
    std::unordered_set<ObjectId>* new_nodes,
    std::function<void(Status, std::unique_ptr<TreeNode::Mutation::Updater>)>
        on_done) {
  std::string next_key;
  if (child_index == node->GetKeyCount()) {
    next_key = "";
  } else {
    Entry entry;
    node->GetEntry(child_index, &entry);
    next_key = entry.key;
  }

  node->GetChild(child_index, [
    next_key = std::move(next_key), page_storage, changes, node_size, new_nodes,
    on_done = std::move(on_done)
  ](Status s, std::unique_ptr<const TreeNode> child) mutable {
    if (s != Status::OK && s != Status::NO_SUCH_CHILD) {
      changes->Next();
      on_done(s, nullptr);
      return;
    }
    if (s == Status::NO_SUCH_CHILD) {
      if ((*changes)->deleted) {
        // We try to remove an entry that is not in the tree. This is
        // expected, as journals collate all operations on a key in a single
        // change: if one does a put then a delete on a key, then we will only
        // see here the delete operation.
        FTL_VLOG(1) << "Failed to delete key " << (*changes)->entry.key
                    << ": No such entry.";
        changes->Next();
        on_done(Status::OK, nullptr);
        return;
      }
      // Add the entry here. Since there is no child, both the new left and
      // right children are empty.
      Entry entry = std::move((*changes)->entry);
      changes->Next();
      on_done(Status::OK,
              std::make_unique<TreeNode::Mutation::Updater>([entry = std::move(
                                                                 entry)](
                  TreeNode::Mutation * mutation) {
                mutation->AddEntry(entry, "", "");
              }));
      return;
    }
    // Recursively search for the key in the child and then update the child
    // id in this node in the corresponding index.
    ApplyChangesIn(page_storage, changes, std::move(child), false,
                   std::move(next_key), node_size, new_nodes,
                   [on_done = std::move(on_done)](
                       Status s, ObjectId new_child_id,
                       std::unique_ptr<TreeNode::Mutation::Updater>
                           parent_updater) mutable {
                     // No need to call Next on the iterator here, it has
                     // already advanced in the ApplyChangesIn loop.
                     on_done(s, std::move(parent_updater));
                   });
  });
}

// Helper function for |ApplyChangesIn|. Allows to iterate over |changes|
// recursively.
void ApplyChangesInRecursive(
    PageStorage* page_storage,
    Iterator<const EntryChange>* changes,
    const TreeNode* node,
    std::string max_key,
    size_t node_size,
    std::unordered_set<ObjectId>* new_nodes,
    std::vector<std::unique_ptr<TreeNode::Mutation::Updater>>* updaters,
    std::function<void(Status)> on_done) {
  // Apply all changes in the correct range: until the max_key. Wait for all
  // changes to be detected for this node before applying them in this node's
  // mutation, so as to guarantee they are applied in the right order.

  if (!changes->Valid() ||
      (!max_key.empty() && (*changes)->entry.key >= max_key)) {
    on_done(Status::OK);
    return;
  }

  auto callback = callback::MakeAsynchronous([
    page_storage, changes, node, max_key = std::move(max_key), node_size,
    new_nodes, updaters, on_done = std::move(on_done)
  ](Status status,
    std::unique_ptr<TreeNode::Mutation::Updater> updater) mutable {
    if (status != Status::OK) {
      on_done(status);
      return;
    }
    updaters->push_back(std::move(updater));
    ApplyChangesInRecursive(page_storage, changes, node, max_key, node_size,
                            new_nodes, updaters, std::move(on_done));
  });

  int index;
  Status s = node->FindKeyOrChild((*changes)->entry.key, &index);
  const EntryChange& change = **changes;

  if (s == Status::OK) {
    // The key was found. Apply the change to this node.
    ApplyChangeOnNode(
        page_storage, &change, node, index, new_nodes,
        [ changes, callback = std::move(callback) ](
            Status status,
            std::unique_ptr<TreeNode::Mutation::Updater> updater) mutable {
          changes->Next();
          callback(status, std::move(updater));
        });
  } else if (s == Status::NOT_FOUND) {
    // The key was not found here. Search in the corresponding child.
    ApplyChangeOnKeyNotFound(page_storage, changes, node, index, node_size,
                             new_nodes, std::move(callback));
  } else {
    // Error in FindKeyOrChild.
    on_done(s);
  }
}

// Applies all given changes in the subtree having |node| as a root.
// |changes| should be sorted by the changes' entry key.
// |max_key| is the maximal value (exclusive) this code could have as a key.
// E.g. a child node placed between keys "A" and "B", has "B" as it's |max_key|.
// It should be an empty string for the root node.
// |node_size| is the maximal size of a tree node as defined in this B-Tree.
// |new_nodes| is the set of all nodes added during the recursion.
// |on_done| is called once, with the returned status and, when successfull, the
// id of the new root and the TreeNode::Mutation::Updater for the parent node's
// mutation.
void ApplyChangesIn(
    PageStorage* page_storage,
    Iterator<const EntryChange>* changes,
    std::unique_ptr<const TreeNode> node,
    bool is_root,
    std::string max_key,
    size_t node_size,
    std::unordered_set<ObjectId>* new_nodes,
    std::function<void(Status,
                       ObjectId,
                       std::unique_ptr<TreeNode::Mutation::Updater>)> on_done) {
  auto updates = std::make_unique<
      std::vector<std::unique_ptr<TreeNode::Mutation::Updater>>>();
  auto updates_ptr = updates.get();
  auto node_ptr = node.get();

  ApplyChangesInRecursive(
      page_storage, changes, node_ptr, max_key, node_size, new_nodes,
      updates_ptr, ftl::MakeCopyable([
        node = std::move(node), is_root, max_key, node_size, new_nodes,
        updates = std::move(updates), on_done = std::move(on_done)
      ](Status s) mutable {
        if (s != Status::OK) {
          on_done(s, "", nullptr);
          return;
        }
        TreeNode::Mutation mutation = node->StartMutation();
        for (const auto& update : *updates) {
          if (update) {
            (*update)(&mutation);
          }
        }
        mutation.Finish(node_size, is_root, max_key, new_nodes,
                        ftl::MakeCopyable(std::move(on_done)));
      }));
}

// Returns a vector with all the tree's entries, sorted by key.
void GetEntriesVector(
    PageStorage* page_storage,
    ObjectIdView root_id,
    std::function<void(Status, std::unique_ptr<std::vector<Entry>>)> on_done) {
  auto entries = std::make_unique<std::vector<Entry>>();
  auto on_next = [entries = entries.get()](EntryAndNodeId e) {
    entries->push_back(e.entry);
    return true;
  };
  btree::ForEachEntry(
      page_storage, root_id, "", on_next, ftl::MakeCopyable([
        entries = std::move(entries), on_done = std::move(on_done)
      ](Status s) mutable {
        if (s != Status::OK) {
          on_done(s, nullptr);
          return;
        }
        on_done(Status::OK, std::move(entries));
      }));
}

// If the |node_id| is empty, creates an empty node and calls the callback with
// that node's id. Otherwise, calls the callback with the given |node_id|.
void GetOrCreateEmptyNode(PageStorage* page_storage,
                          ObjectIdView node_id,
                          std::function<void(Status, ObjectId)> callback) {
  if (!node_id.empty()) {
    callback(Status::OK, node_id.ToString());
    return;
  }
  TreeNode::Empty(page_storage, ftl::MakeCopyable(std::move(callback)));
}

}  // namespace

void ApplyChanges(
    PageStorage* page_storage,
    ObjectIdView root_id,
    size_t node_size,
    std::unique_ptr<Iterator<const EntryChange>> changes,
    std::function<void(Status, ObjectId, std::unordered_set<ObjectId>)>
        callback) {
  // Get or create the root.
  GetOrCreateEmptyNode(
      page_storage, root_id, ftl::MakeCopyable([
        page_storage, node_size, changes = std::move(changes),
        callback = std::move(callback)
      ](Status s, ObjectId root_id) mutable {
        if (s != Status::OK) {
          callback(s, "", {});
          return;
        }
        TreeNode::FromId(
            page_storage, root_id, ftl::MakeCopyable([
              page_storage, node_size, changes = std::move(changes),
              callback = std::move(callback)
            ](Status s, std::unique_ptr<const TreeNode> root) mutable {
              if (s != Status::OK) {
                callback(s, "", {});
                return;
              }
              // |new_nodes| will be populated with all nodes created after this
              // set of changes.
              auto new_nodes = std::make_unique<std::unordered_set<ObjectId>>();
              std::unordered_set<ObjectId>* new_nodes_ptr = new_nodes.get();
              auto changes_ptr = changes.get();
              ApplyChangesIn(
                  page_storage, changes_ptr, std::move(root), true, "",
                  node_size, new_nodes_ptr,
                  ftl::MakeCopyable([
                    new_nodes = std::move(new_nodes),
                    callback = std::move(callback), changes = std::move(changes)
                  ](Status s, ObjectId new_id,
                    std::unique_ptr<TreeNode::Mutation::Updater>
                        parent_updater) mutable {
                    FTL_DCHECK(parent_updater == nullptr);
                    if (s != Status::OK) {
                      callback(s, "", {});
                      return;
                    }
                    callback(Status::OK, std::move(new_id),
                             std::move(*new_nodes));
                  }));
            }));
      }));
}

void GetObjectIds(PageStorage* page_storage,
                  ObjectIdView root_id,
                  std::function<void(Status, std::set<ObjectId>)> callback) {
  FTL_DCHECK(!root_id.empty());
  auto object_ids = std::make_unique<std::set<ObjectId>>();
  object_ids->insert(root_id.ToString());

  auto on_next = [object_ids = object_ids.get()](EntryAndNodeId e) {
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
  ftl::RefPtr<callback::Waiter<Status, std::unique_ptr<const Object>>> waiter_ =
      callback::Waiter<Status, std::unique_ptr<const Object>>::Create(
          Status::OK);
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
  TreeNode::FromId(page_storage, root_id, [
    min_key = std::move(min_key), page_storage, on_next = std::move(on_next),
    on_done = std::move(on_done)
  ](Status status, std::unique_ptr<const TreeNode> root) {
    if (status != Status::OK) {
      on_done(status);
      return;
    }
    ForEachEntryInSubtree(
        page_storage, std::move(root), std::move(min_key),
        std::move(on_next), [on_done = std::move(on_done)](Status s, bool) {
          on_done(s);
        });

  });
}

void ForEachDiff(PageStorage* page_storage,
                 ObjectIdView base_root_id,
                 ObjectIdView other_root_id,
                 std::function<bool(EntryChange)> on_next,
                 std::function<void(Status)> on_done) {
  // TODO(nellyv): This is a naive calculation of the the diff, loading all
  // entries from both versions in memory and then computing the diff. This
  // should be updated with the new version of the BTree.
  auto waiter =
      callback::Waiter<Status, std::unique_ptr<std::vector<Entry>>>::Create(
          Status::OK);
  GetEntriesVector(page_storage, base_root_id, waiter->NewCallback());
  GetEntriesVector(page_storage, other_root_id, waiter->NewCallback());
  waiter->Finalize([
    on_next = std::move(on_next), on_done = std::move(on_done)
  ](Status s, std::vector<std::unique_ptr<std::vector<Entry>>> entries) {
    if (s != Status::OK) {
      on_done(s);
      return;
    }
    FTL_DCHECK(entries.size() == 2u);
    auto base_it = entries[0].get()->begin();
    auto base_it_end = entries[0].get()->end();
    auto other_it = entries[1].get()->begin();
    auto other_it_end = entries[1].get()->end();

    while (base_it != base_it_end && other_it != other_it_end) {
      if (*base_it == *other_it) {
        // Entries are equal.
        ++base_it;
        ++other_it;
        continue;
      }
      EntryChange change;
      // strcmp will not work if keys contain '\0' characters.
      int cmp = ftl::StringView(base_it->key).compare(other_it->key);
      if (cmp >= 0) {
        // The entry was added or updated.
        change = {*other_it, false};
      } else {
        // The entry was deleted.
        change = {*base_it, true};
      }
      if (!on_next(std::move(change))) {
        on_done(Status::OK);
        return;
      }
      // Advance the iterators.
      if (cmp >= 0) {
        ++other_it;
      }
      if (cmp <= 0) {
        ++base_it;
      }
    }
    while (base_it != base_it_end) {
      // The entry was deleted.
      EntryChange change{*base_it, true};
      if (!on_next(std::move(change))) {
        on_done(Status::OK);
        return;
      }
      base_it++;
    }
    while (other_it != other_it_end) {
      // The entry was added.
      EntryChange change{*other_it, false};
      if (!on_next(std::move(change))) {
        on_done(Status::OK);
        return;
      }
      other_it++;
    }
    on_done(Status::OK);
  });
}

}  // namespace btree
}  // namespace storage
