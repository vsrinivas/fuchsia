// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/storage/fake/fake_commit.h"

#include <memory>
#include <string>

#include "apps/ledger/src/storage/fake/fake_journal_delegate.h"
#include "apps/ledger/src/storage/public/commit.h"
#include "apps/ledger/src/storage/public/iterator.h"

namespace storage {
namespace fake {
namespace {
class EntryMapIterator : public Iterator<const storage::Entry> {
 public:
  EntryMapIterator(
      std::map<std::string, fake::FakeJournalDelegate::Entry>::const_iterator
          it,
      std::map<std::string, fake::FakeJournalDelegate::Entry>::const_iterator
          end)
      : it_(it), end_(end) {
    if (it_ != end_) {
      entry_.key = it_->first;
      entry_.object_id = it_->second.value;
      entry_.priority = it_->second.priority;
    }
  }

  ~EntryMapIterator() {}

  Iterator<const storage::Entry>& Next() override {
    FTL_DCHECK(Valid()) << "Iterator::Next iterator not valid";
    ++it_;
    while (it_ != end_ && it_->second.deleted) {
      ++it_;
    }
    if (it_ != end_) {
      entry_.key = it_->first;
      entry_.object_id = it_->second.value;
      entry_.priority = it_->second.priority;
    }
    return *this;
  }

  bool Valid() const override { return it_ != end_; }

  Status GetStatus() const override { return Status::OK; }

  const storage::Entry& operator*() const override { return entry_; }
  const storage::Entry* operator->() const override { return &entry_; }

 private:
  storage::Entry entry_;
  std::map<std::string, fake::FakeJournalDelegate::Entry>::const_iterator it_;
  std::map<std::string, fake::FakeJournalDelegate::Entry>::const_iterator end_;
};
}

FakeCommit::FakeCommit(FakeJournalDelegate* journal) : journal_(journal) {}
FakeCommit::~FakeCommit() {}

std::unique_ptr<Commit> FakeCommit::Clone() const {
  return std::make_unique<FakeCommit>(journal_);
}

const CommitId& FakeCommit::GetId() const {
  return journal_->GetId();
}

std::vector<CommitIdView> FakeCommit::GetParentIds() const {
  return std::vector<CommitIdView>();
}

int64_t FakeCommit::GetTimestamp() const {
  return 0;
}

uint64_t FakeCommit::GetGeneration() const {
  return 0;
}

ObjectIdView FakeCommit::GetRootId() const {
  return journal_->GetId();
}

ftl::StringView FakeCommit::GetStorageBytes() const {
  return ftl::StringView();
}

}  // namespace fake
}  // namespace storage
