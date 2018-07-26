// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/storage/fake/fake_journal_delegate.h"

#include <utility>

#include <lib/fit/function.h>
#include <zircon/syscalls.h>

#include "peridot/bin/ledger/storage/fake/fake_commit.h"
#include "peridot/bin/ledger/storage/public/constants.h"

namespace storage {
namespace fake {
namespace {

storage::CommitId RandomCommitId() {
  std::string result;
  result.resize(kCommitIdSize);
  zx_cprng_draw(&result[0], kCommitIdSize);
  return result;
}

}  // namespace

FakeJournalDelegate::FakeJournalDelegate(CommitId parent_id, bool autocommit,
                                         uint64_t generation = 0)
    : autocommit_(autocommit),
      id_(RandomCommitId()),
      parent_id_(std::move(parent_id)),
      generation_(generation) {}

FakeJournalDelegate::FakeJournalDelegate(CommitId parent_id, CommitId other_id,
                                         bool autocommit,
                                         uint64_t generation = 0)
    : autocommit_(autocommit),
      id_(RandomCommitId()),
      parent_id_(std::move(parent_id)),
      other_id_(std::move(other_id)),
      generation_(generation) {}

FakeJournalDelegate::~FakeJournalDelegate() {}

Status FakeJournalDelegate::SetValue(convert::ExtendedStringView key,
                                     ObjectIdentifier value,
                                     KeyPriority priority) {
  if (is_committed_ || is_rolled_back_) {
    return Status::ILLEGAL_STATE;
  }
  Get(key).value = value;
  Get(key).priority = priority;
  return Status::OK;
}

Status FakeJournalDelegate::Delete(convert::ExtendedStringView key) {
  if (is_committed_ || is_rolled_back_) {
    return Status::ILLEGAL_STATE;
  }
  Get(key).deleted = true;
  return Status::OK;
}

void FakeJournalDelegate::Commit(
    fit::function<void(Status, std::unique_ptr<const storage::Commit>)>
        callback) {
  if (is_committed_ || is_rolled_back_) {
    callback(Status::ILLEGAL_STATE, nullptr);
    return;
  }

  commit_callback_ = std::move(callback);

  if (autocommit_) {
    ResolvePendingCommit(Status::OK);
  }
}

bool FakeJournalDelegate::IsCommitted() const { return is_committed_; }

Status FakeJournalDelegate::Rollback() {
  if (is_committed_ || is_rolled_back_) {
    return Status::ILLEGAL_STATE;
  }
  is_rolled_back_ = true;
  return Status::OK;
}

bool FakeJournalDelegate::IsRolledBack() const { return is_rolled_back_; }

std::vector<CommitIdView> FakeJournalDelegate::GetParentIds() const {
  if (other_id_.empty()) {
    return {parent_id_};
  }
  return {parent_id_, other_id_};
}

bool FakeJournalDelegate::IsPendingCommit() {
  return static_cast<bool>(commit_callback_);
}

void FakeJournalDelegate::ResolvePendingCommit(Status /*status*/) {
  is_committed_ = true;
  auto callback = std::move(commit_callback_);
  commit_callback_ = nullptr;
  callback(Status::OK, std::make_unique<const FakeCommit>(this));
}

const std::map<std::string, FakeJournalDelegate::Entry,
               convert::StringViewComparator>&
FakeJournalDelegate::GetData() const {
  return data_;
}

FakeJournalDelegate::Entry& FakeJournalDelegate::Get(
    convert::ExtendedStringView key) {
  auto it = data_.find(key);
  if (it != data_.end())
    return it->second;
  return data_[key.ToString()];
}

}  // namespace fake
}  // namespace storage
