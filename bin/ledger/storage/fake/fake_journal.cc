// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/storage/fake/fake_journal.h"

#include <string>

namespace storage {
namespace fake {

FakeJournal::FakeJournal(FakeJournalDelegate* delegate) : delegate_(delegate) {}

FakeJournal::~FakeJournal() {}

void FakeJournal::Commit(
    std::function<void(Status, std::unique_ptr<const storage::Commit>)>
        callback) {
  delegate_->Commit(callback);
}

Status FakeJournal::Rollback() {
  return delegate_->Rollback();
}

void FakeJournal::Put(convert::ExtendedStringView key,
                      ObjectIdView object_id,
                      KeyPriority priority,
                      std::function<void(Status)> callback) {
  callback(delegate_->SetValue(key, object_id, priority));
}

void FakeJournal::Delete(convert::ExtendedStringView key,
                         std::function<void(Status)> callback) {
  callback(delegate_->Delete(key));
}

const JournalId& FakeJournal::GetId() const {
  return delegate_->GetId();
}

}  // namespace fake
}  // namespace storage
