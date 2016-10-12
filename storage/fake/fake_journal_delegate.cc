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

Status FakeJournalDelegate::SetValue(const std::string& key,
                                     ObjectIdView value,
                                     KeyPriority priority) {
  if (is_committed_ || is_rolled_back_) {
    return Status::ILLEGAL_STATE;
  }
  data_[key].value = value.ToString();
  data_[key].priority = priority;
  return Status::OK;
}

Status FakeJournalDelegate::Delete(const std::string& key) {
  if (is_committed_ || is_rolled_back_) {
    return Status::ILLEGAL_STATE;
  }
  data_[key].deleted = true;
  return Status::OK;
}

Status FakeJournalDelegate::Commit() {
  if (is_committed_ || is_rolled_back_) {
    return Status::ILLEGAL_STATE;
  }
  is_committed_ = true;
  return Status::OK;
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

const std::map<std::string, FakeJournalDelegate::Entry>
FakeJournalDelegate::GetData() const {
  return data_;
}

}  // namespace fake
}  // namespace storage
