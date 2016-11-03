// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/storage/impl/btree/diff_iterator.h"

#include "apps/ledger/storage/impl/btree/commit_contents_impl.h"
#include "apps/ledger/storage/impl/btree/tree_node.h"
#include "lib/ftl/logging.h"

namespace storage {

DiffIterator::DiffIterator(std::unique_ptr<const TreeNode> left,
                           std::unique_ptr<const TreeNode> right)
    : left_(new BTreeIterator(std::move(left))),
      right_(new BTreeIterator(std::move(right))) {
  if (left_->Valid() && right_->Valid() && **left_ == **right_) {
    Advance();
  } else if (left_->Valid() || right_->Valid()) {
    BuildEntryChange();
  }
}

DiffIterator::~DiffIterator() {}

DiffIterator& DiffIterator::Next() {
  FTL_DCHECK(Valid());
  FTL_DCHECK(!changes_.empty());

  changes_.pop();

  if (!changes_.empty()) {
    return *this;
  }

  Advance();

  return *this;
}

void DiffIterator::Advance() {
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

  while (left_->Valid() && right_->Valid() && **left_ == **right_) {
    left_->Next();
    right_->Next();
  }
  if (Valid()) {
    BuildEntryChange();
  }
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
  if ((left_->Valid() && !right_->Valid()) || (*left_)->key < (*right_)->key) {
    EntryChange entry_change;
    entry_change.entry = **left_;
    entry_change.deleted = true;
    changes_.push(entry_change);
  } else if ((right_->Valid() && !left_->Valid()) ||
             (*left_)->key > (*right_)->key) {
    EntryChange entry_change;
    entry_change.entry = **right_;
    entry_change.deleted = false;
    changes_.push(entry_change);
  } else {
    EntryChange entry_change1;
    entry_change1.entry = **left_;
    entry_change1.deleted = true;
    changes_.push(entry_change1);

    EntryChange entry_change2;
    entry_change2.entry = **right_;
    entry_change2.deleted = false;
    changes_.push(entry_change2);
  }
}
const EntryChange& DiffIterator::operator*() const {
  return changes_.front();
}

const EntryChange* DiffIterator::operator->() const {
  return &(changes_.front());
}

}  // namespace storage
