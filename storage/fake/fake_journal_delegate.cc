// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/storage/fake/fake_journal_delegate.h"

#include "apps/ledger/glue/crypto/rand.h"
#include "apps/ledger/storage/public/constants.h"

namespace storage {
namespace fake {
namespace {

storage::CommitId RandomId() {
  std::string result;
  result.resize(kCommitIdSize);
  glue::RandBytes(&result[0], kCommitIdSize);
  return result;
}

}  // namespace

FakeJournalDelegate::FakeJournalDelegate() : id_(RandomId()) {}

FakeJournalDelegate::~FakeJournalDelegate() {}

Status FakeJournalDelegate::SetValue(convert::ExtendedStringView key,
                                     ObjectIdView value,
                                     KeyPriority priority) {
  if (is_committed_ || is_rolled_back_) {
    return Status::ILLEGAL_STATE;
  }
  Get(key).value = value.ToString();
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
    std::function<void(Status, const CommitId&)> callback) {
  if (is_committed_ || is_rolled_back_) {
    callback(Status::ILLEGAL_STATE, "");
    return;
  }
  is_committed_ = true;
  callback(Status::OK, id_);
}

bool FakeJournalDelegate::IsCommitted() const {
  return is_committed_;
}

Status FakeJournalDelegate::Rollback() {
  if (is_committed_ || is_rolled_back_) {
    return Status::ILLEGAL_STATE;
  }
  is_rolled_back_ = true;
  return Status::OK;
}

bool FakeJournalDelegate::IsRolledBack() const {
  return is_rolled_back_;
}

const std::
    map<std::string, FakeJournalDelegate::Entry, convert::StringViewComparator>&
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
