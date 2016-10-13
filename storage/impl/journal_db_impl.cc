// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/storage/impl/journal_db_impl.h"

#include <string>

#include "apps/ledger/storage/impl/db.h"

namespace storage {

JournalDBImpl::JournalDBImpl(DB* db, const JournalId& id, const CommitId& base)
    : db_(db), id_(id), base_(base), valid_(true) {}

JournalDBImpl::~JournalDBImpl() {}

std::unique_ptr<Journal> JournalDBImpl::Simple(DB* db,
                                               const JournalId& id,
                                               const CommitId& base) {
  return std::unique_ptr<Journal>(new JournalDBImpl(db, id, base));
}

std::unique_ptr<Journal> JournalDBImpl::Merge(DB* db,
                                              const JournalId& id,
                                              const CommitId& base,
                                              const CommitId& other) {
  JournalDBImpl* db_journal = new JournalDBImpl(db, id, base);
  db_journal->other_ = std::unique_ptr<CommitId>(new std::string(other));
  std::unique_ptr<Journal> journal(db_journal);
  return journal;
}

JournalId JournalDBImpl::GetId() const {
  return id_;
}

Status JournalDBImpl::Put(convert::ExtendedStringView key,
                          ObjectIdView blob_id,
                          KeyPriority priority) {
  if (!valid_) {
    return Status::ILLEGAL_STATE;
  }
  return db_->AddJournalEntry(id_, key, blob_id, priority);
}

Status JournalDBImpl::Delete(convert::ExtendedStringView key) {
  if (!valid_) {
    return Status::ILLEGAL_STATE;
  }
  return db_->RemoveJournalEntry(id_, key);
}

Status JournalDBImpl::Commit(CommitId* commit_id) {
  return Status::NOT_IMPLEMENTED;
}

Status JournalDBImpl::Rollback() {
  Status s = db_->RemoveJournal(id_);
  if (s == Status::OK) {
    valid_ = false;
  }
  return s;
}

}  // namespace storage
