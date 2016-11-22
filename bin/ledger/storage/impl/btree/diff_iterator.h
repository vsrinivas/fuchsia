// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_SRC_STORAGE_IMPL_BTREE_DIFF_ITERATOR_H_
#define APPS_LEDGER_SRC_STORAGE_IMPL_BTREE_DIFF_ITERATOR_H_

#include <memory>
#include <queue>

#include "apps/ledger/src/storage/impl/btree/position.h"
#include "apps/ledger/src/storage/impl/btree/tree_node.h"
#include "apps/ledger/src/storage/public/commit_contents.h"
#include "apps/ledger/src/storage/public/iterator.h"
#include "lib/ftl/macros.h"

namespace storage {

// An iterator over the differences between an ordered pair of BTrees,
// represented by thier roots. Differences are computed in the |left| to |right|
// direction (|left| is the base for the diff, |right| the target).
class DiffIterator : public Iterator<const EntryChange> {
 public:
  DiffIterator(std::unique_ptr<const TreeNode> left,
               std::unique_ptr<const TreeNode> right);
  ~DiffIterator() override;

  // Iterator:
  DiffIterator& Next() override;
  bool Valid() const override;
  Status GetStatus() const override;

  const EntryChange& operator*() const override;
  const EntryChange* operator->() const override;

 private:
  void BuildEntryChange();

  // Stores the change of the B-Trees at the current position of the iterator.
  // This is used as a staging area for operator* and operator-> calls.
  std::unique_ptr<EntryChange> change_;

  // TODO(etiennej): We are going for the simplest, most naive implementation as
  // to unblock further work. Better implementations may want to inspect the
  // individual nodes inside B-Trees to skip identical nodes and branches
  // altogether instead of iterating through them.
  std::unique_ptr<Iterator<const Entry>> left_;
  std::unique_ptr<Iterator<const Entry>> right_;
};

}  // namespace storage

#endif  // APPS_LEDGER_SRC_STORAGE_IMPL_BTREE_DIFF_ITERATOR_H_
