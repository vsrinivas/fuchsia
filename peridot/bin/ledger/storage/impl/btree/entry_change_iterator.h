// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_STORAGE_IMPL_BTREE_ENTRY_CHANGE_ITERATOR_H_
#define PERIDOT_BIN_LEDGER_STORAGE_IMPL_BTREE_ENTRY_CHANGE_ITERATOR_H_

#include <vector>

#include "peridot/bin/ledger/storage/public/iterator.h"
#include "peridot/bin/ledger/storage/public/types.h"

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

#endif  // PERIDOT_BIN_LEDGER_STORAGE_IMPL_BTREE_ENTRY_CHANGE_ITERATOR_H_
