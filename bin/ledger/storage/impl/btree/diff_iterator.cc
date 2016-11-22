// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/storage/impl/btree/diff_iterator.h"

#include "apps/ledger/src/storage/impl/btree/commit_contents_impl.h"
#include "apps/ledger/src/storage/impl/btree/tree_node.h"
#include "lib/ftl/logging.h"

namespace storage {

DiffIterator::DiffIterator(std::unique_ptr<const TreeNode> left,
                           std::unique_ptr<const TreeNode> right)
    : left_(std::make_unique<BTreeIterator>(std::move(left))),
      right_(std::make_unique<BTreeIterator>(std::move(right))) {
  if (!Valid()) {
    return;
  }

  if (left_->Valid() && right_->Valid() && **left_ == **right_) {
    Next();
  } else {
    BuildEntryChange();
  }
}

DiffIterator::~DiffIterator() {}

DiffIterator& DiffIterator::Next() {
  FTL_DCHECK(Valid());

  // Unconditionnaly advance by one step.
  if (left_->Valid() && !right_->Valid()) {
    left_->Next();
  } else if (right_->Valid() && !left_->Valid()) {
    right_->Next();
  } else if ((*left_)->key < (*right_)->key) {
    left_->Next();
  } else if ((*left_)->key > (*right_)->key) {
    right_->Next();
  } else {
    left_->Next();
    right_->Next();
  }

  // While the two iterators point to the same data, advance until finding a
  // difference.
  while (left_->Valid() && right_->Valid() && **left_ == **right_) {
    left_->Next();
    right_->Next();
  }
  if (Valid()) {
    BuildEntryChange();
  }
  return *this;
}

bool DiffIterator::Valid() const {
  return (left_->Valid() || right_->Valid()) &&
         left_->GetStatus() == Status::OK && right_->GetStatus() == Status::OK;
}

Status DiffIterator::GetStatus() const {
  if (left_->GetStatus() != Status::OK) {
    return left_->GetStatus();
  }
  return right_->GetStatus();
}

void DiffIterator::BuildEntryChange() {
  FTL_DCHECK(Valid());
  if ((left_->Valid() && !right_->Valid()) ||
      (left_->Valid() && right_->Valid() && (*left_)->key < (*right_)->key)) {
    change_.reset(new EntryChange{**left_, true});
  } else {
    change_.reset(new EntryChange{**right_, false});
  }
}
const EntryChange& DiffIterator::operator*() const {
  return *change_;
}

const EntryChange* DiffIterator::operator->() const {
  return change_.get();
}

}  // namespace storage
