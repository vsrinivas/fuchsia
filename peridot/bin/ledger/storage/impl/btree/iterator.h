// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_STORAGE_IMPL_BTREE_ITERATOR_H_
#define PERIDOT_BIN_LEDGER_STORAGE_IMPL_BTREE_ITERATOR_H_

#include <functional>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <lib/fit/function.h>

#include "peridot/bin/ledger/coroutine/coroutine.h"
#include "peridot/bin/ledger/storage/impl/btree/synchronous_storage.h"
#include "peridot/bin/ledger/storage/impl/btree/tree_node.h"
#include "peridot/bin/ledger/storage/public/types.h"

namespace storage {
namespace btree {

// An entry and the id of the tree node in which it is stored.
struct EntryAndNodeIdentifier {
  const Entry& entry;                       // NOLINT
  const ObjectIdentifier& node_identifier;  // NOLINT
};

// Iterator over a B-Tree. This iterator exposes the internal of the iteration
// to allow to skip part of the tree.
class BTreeIterator {
 public:
  explicit BTreeIterator(SynchronousStorage* storage);

  BTreeIterator(BTreeIterator&& other) noexcept;
  BTreeIterator& operator=(BTreeIterator&& other) noexcept;

  // Initializes the iterator with the root node of the tree.
  Status Init(ObjectIdentifier node_identifier);

  // Skips the iteration until the first key that is greater than or equal to
  // |min_key|.
  Status SkipTo(fxl::StringView min_key);

  // Skips to the index where key could be found, within the current node. The
  // current index will only be updated if the new index is after the current
  // one. Returns true if either the key was found in this node, or if it is
  // guaranteed not to be found in any of this nodes children; false otherwise.
  bool SkipToIndex(fxl::StringView key);

  // Returns the identifier of the next child that will be explored, or
  // |nullptr| if it doesn't exist.
  const ObjectIdentifier* GetNextChild() const;

  // Returns whether the iterator is currently on a value. The method
  // |CurrentEntry| is only valid when |HasValue| is true.
  bool HasValue() const;

  // Returns whether the iteration is finished.
  bool Finished() const;

  // Returns the current value of the iterator. It is only valid when
  // |HasValue| is true.
  const Entry& CurrentEntry() const;

  // Returns the identifier of the node at the top of the stack.
  const storage::ObjectIdentifier& GetIdentifier() const;

  // Returns the level of the node at the top of the stack.
  uint8_t GetLevel() const;

  // Advances the iterator by a single step.
  Status Advance();

  // Advances the iterator until it has a value or it finishes.
  Status AdvanceToValue();

  // Skips the next sub tree in the iteration.
  void SkipNextSubTree();

 private:
  size_t& CurrentIndex();
  size_t CurrentIndex() const;
  const TreeNode& CurrentNode() const;
  Status Descend(const ObjectIdentifier& node_identifier);

  SynchronousStorage* storage_;
  // Stack representing the current iteration state. Each level represents the
  // current node in the B-Tree, and the index currently looked at. If
  // |descending_| is |true|, the index is the child index, otherwise it is the
  // entry index.
  std::vector<std::pair<std::unique_ptr<const TreeNode>, size_t>> stack_;
  bool descending_ = true;

  FXL_DISALLOW_COPY_AND_ASSIGN(BTreeIterator);
};

// Retrieves the ids of all objects in the B-Tree, i.e tree nodes and values of
// entries in the tree. After a successfull call, |callback| will be called
// with the set of results.
void GetObjectIdentifiers(
    coroutine::CoroutineService* coroutine_service, PageStorage* page_storage,
    ObjectIdentifier root_identifier,
    fit::function<void(Status, std::set<ObjectIdentifier>)> callback);

// Tries to download all tree nodes and values with |EAGER| priority that are
// not locally available from sync. To do this |PageStorage::GetObject| is
// called for all corresponding objects.
void GetObjectsFromSync(coroutine::CoroutineService* coroutine_service,
                        PageStorage* page_storage,
                        ObjectIdentifier root_identifier,
                        fit::function<void(Status)> callback);

// Iterates through the nodes of the tree with the given root and calls
// |on_next| on found entries with a key equal to or greater than |min_key|. The
// return value of |on_next| can be used to stop the iteration: returning false
// will interrupt the iteration in progress and no more |on_next| calls will be
// made. |on_done| is called once, upon successfull completion, i.e. when there
// are no more elements or iteration was interrupted, or if an error occurs.
void ForEachEntry(coroutine::CoroutineService* coroutine_service,
                  PageStorage* page_storage, ObjectIdentifier root_identifier,
                  std::string min_key,
                  fit::function<bool(EntryAndNodeIdentifier)> on_next,
                  fit::function<void(Status)> on_done);

}  // namespace btree
}  // namespace storage

#endif  // PERIDOT_BIN_LEDGER_STORAGE_IMPL_BTREE_ITERATOR_H_
