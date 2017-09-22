// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_SRC_STORAGE_IMPL_BTREE_ENTRY_CHANGE_ITERATOR_H_
#define APPS_LEDGER_SRC_STORAGE_IMPL_BTREE_ENTRY_CHANGE_ITERATOR_H_

#include <vector>

#include "apps/ledger/src/storage/public/iterator.h"
#include "apps/ledger/src/storage/public/types.h"

namespace storage {
namespace btree {

class EntryChangeIterator : public Iterator<const storage::EntryChange> {
 public:
  EntryChangeIterator(std::vector<EntryChange>::const_iterator it,
                      std::vector<EntryChange>::const_iterator end)
      : it_(it), end_(end) {}

  ~EntryChangeIterator() override {}

  Iterator<const storage::EntryChange>& Next() override {
    FXL_DCHECK(Valid()) << "Iterator::Next iterator not valid";
    ++it_;
    return *this;
  }

  bool Valid() const override { return it_ != end_; }

  Status GetStatus() const override { return Status::OK; }

  const storage::EntryChange& operator*() const override { return *it_; }
  const storage::EntryChange* operator->() const override { return &(*it_); }

 private:
  std::vector<EntryChange>::const_iterator it_;
  std::vector<EntryChange>::const_iterator end_;

  FXL_DISALLOW_COPY_AND_ASSIGN(EntryChangeIterator);
};

}  // namespace btree
}  // namespace storage

#endif  // APPS_LEDGER_SRC_STORAGE_IMPL_BTREE_ENTRY_CHANGE_ITERATOR_H_
