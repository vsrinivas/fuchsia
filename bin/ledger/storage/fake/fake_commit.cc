// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/storage/fake/fake_commit.h"

#include <memory>
#include <string>

#include "apps/ledger/src/storage/fake/fake_journal_delegate.h"
#include "apps/ledger/src/storage/public/commit.h"

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

class FakeCommitContents : public CommitContents {
 public:
  FakeCommitContents(FakeJournalDelegate* journal) : journal_(journal) {}
  ~FakeCommitContents() {}

  // CommitContents:
  std::unique_ptr<Iterator<const Entry>> begin() const override {
    const std::map<std::string, fake::FakeJournalDelegate::Entry,
                   convert::StringViewComparator>& data = journal_->GetData();
    return std::make_unique<EntryMapIterator>(data.begin(), data.end());
  }

  std::unique_ptr<Iterator<const Entry>> find(
      convert::ExtendedStringView key) const override {
    std::unique_ptr<Iterator<const Entry>> it = begin();
    while (it->Valid() && (*it)->key < key) {
      it->Next();
    }
    return it;
  }

  void diff(
      std::unique_ptr<CommitContents> other,
      std::function<void(Status, std::unique_ptr<Iterator<const EntryChange>>)>
          callback) const override {
    callback(Status::NOT_IMPLEMENTED, nullptr);
  }

  ObjectId GetBaseObjectId() const override {
    FTL_NOTIMPLEMENTED();
    return nullptr;
  }

 private:
  FakeJournalDelegate* journal_;
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

std::vector<CommitId> FakeCommit::GetParentIds() const {
  return std::vector<CommitId>();
}

int64_t FakeCommit::GetTimestamp() const {
  return 0;
}

std::unique_ptr<CommitContents> FakeCommit::GetContents() const {
  return std::make_unique<FakeCommitContents>(journal_);
}

ObjectId FakeCommit::FakeCommit::GetRootId() const {
  return journal_->GetId();
}

std::string FakeCommit::GetStorageBytes() const {
  return std::string();
}

}  // namespace fake
}  // namespace storage
