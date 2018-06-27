// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/storage/fake/fake_journal.h"

#include <string>

#include <lib/fit/function.h>

namespace storage {
namespace fake {

FakeJournal::FakeJournal(FakeJournalDelegate* delegate) : delegate_(delegate) {}

FakeJournal::~FakeJournal() {}

void FakeJournal::Commit(
    fit::function<void(Status, std::unique_ptr<const storage::Commit>)>
        callback) {
  delegate_->Commit(std::move(callback));
}

Status FakeJournal::Rollback() { return delegate_->Rollback(); }

void FakeJournal::Put(convert::ExtendedStringView key,
                      ObjectIdentifier object_identifier, KeyPriority priority,
                      fit::function<void(Status)> callback) {
  callback(delegate_->SetValue(key, std::move(object_identifier), priority));
}

void FakeJournal::Delete(convert::ExtendedStringView key,
                         fit::function<void(Status)> callback) {
  callback(delegate_->Delete(key));
}

const JournalId& FakeJournal::GetId() const { return delegate_->GetId(); }

}  // namespace fake
}  // namespace storage
