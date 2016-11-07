// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_SRC_STORAGE_IMPL_BTREE_BTREE_ITERATOR_H_
#define APPS_LEDGER_SRC_STORAGE_IMPL_BTREE_BTREE_ITERATOR_H_

#include <memory>
#include <stack>

#include "apps/ledger/src/convert/convert.h"
#include "apps/ledger/src/storage/impl/btree/position.h"
#include "apps/ledger/src/storage/impl/btree/tree_node.h"
#include "apps/ledger/src/storage/public/commit_contents.h"
#include "apps/ledger/src/storage/public/iterator.h"
#include "lib/ftl/macros.h"

namespace storage {

// An iterator over a BTree, represented by its root.
class BTreeIterator : public Iterator<const Entry> {
 public:
  explicit BTreeIterator(std::unique_ptr<const TreeNode> root);
  ~BTreeIterator() override;

  // Iterator:
  BTreeIterator& Next() override;
  bool Valid() const override;
  Status GetStatus() const override;

  // Advances the iterator to the entry equal or after the provided key.
  BTreeIterator& Seek(convert::ExtendedStringView key);

  const Entry& operator*() const override;
  const Entry* operator->() const override;

 private:
  std::stack<Position> stack_;
  Entry current_entry_;
  Status current_status_ = Status::OK;
};

}  // namespace storage

#endif  // APPS_LEDGER_SRC_STORAGE_IMPL_BTREE_BTREE_ITERATOR_H_
