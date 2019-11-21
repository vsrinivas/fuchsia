// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/storage/fake/fake_journal_delegate.h"

#include <lib/fit/function.h>
#include <zircon/syscalls.h>

#include <utility>

#include "src/ledger/bin/storage/fake/fake_commit.h"
#include "src/ledger/bin/storage/public/constants.h"
#include "src/ledger/lib/convert/convert.h"

namespace storage {
namespace fake {
namespace {

storage::CommitId RandomCommitId(rng::Random* random) {
  storage::CommitId result;
  result.resize(kCommitIdSize);
  random->Draw(&result);
  return result;
}

}  // namespace

FakeJournalDelegate::FakeJournalDelegate(rng::Random* random, FakeObjectIdentifierFactory* factory,
                                         Data initial_data, CommitId parent_id, bool autocommit,
                                         uint64_t generation = 0)
    : autocommit_(autocommit),
      id_(RandomCommitId(random)),
      parent_id_(std::move(parent_id)),
      data_(std::move(initial_data)),
      generation_(generation),
      factory_(factory) {}

FakeJournalDelegate::FakeJournalDelegate(rng::Random* random, FakeObjectIdentifierFactory* factory,
                                         Data initial_data, CommitId parent_id, CommitId other_id,
                                         bool autocommit, uint64_t generation = 0)
    : autocommit_(autocommit),
      id_(RandomCommitId(random)),
      parent_id_(std::move(parent_id)),
      other_id_(std::move(other_id)),
      data_(std::move(initial_data)),
      generation_(generation),
      factory_(factory) {}

FakeJournalDelegate::~FakeJournalDelegate() = default;

void FakeJournalDelegate::SetValue(convert::ExtendedStringView key, ObjectIdentifier value,
                                   KeyPriority priority) {
  FXL_DCHECK(!is_committed_);
  data_.insert(
      {convert::ToString(key),
       {convert::ToString(key), std::move(value), priority, EntryId(convert::ToString(key))}});
}

void FakeJournalDelegate::Delete(convert::ExtendedStringView key) {
  FXL_DCHECK(!is_committed_);
  auto it = data_.find(key);
  if (it != data_.end()) {
    data_.erase(it);
  }
}

void FakeJournalDelegate::Clear() {
  FXL_DCHECK(!is_committed_);
  data_.clear();
}

void FakeJournalDelegate::Commit(
    fit::function<void(Status, std::unique_ptr<const storage::Commit>)> callback) {
  if (is_committed_) {
    callback(Status::ILLEGAL_STATE, nullptr);
    return;
  }

  commit_callback_ = std::move(callback);

  if (autocommit_) {
    ResolvePendingCommit(Status::OK);
  }
}

bool FakeJournalDelegate::IsCommitted() const { return is_committed_; }

std::vector<CommitIdView> FakeJournalDelegate::GetParentIds() const {
  if (other_id_.empty()) {
    return {parent_id_};
  }
  return {parent_id_, other_id_};
}

bool FakeJournalDelegate::IsPendingCommit() { return static_cast<bool>(commit_callback_); }

void FakeJournalDelegate::ResolvePendingCommit(Status /*status*/) {
  is_committed_ = true;
  auto callback = std::move(commit_callback_);
  commit_callback_ = nullptr;
  callback(Status::OK, std::make_unique<const FakeCommit>(this, factory_));
}

const FakeJournalDelegate::Data& FakeJournalDelegate::GetData() const { return data_; }

}  // namespace fake
}  // namespace storage
