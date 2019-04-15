// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/storage/fake/fake_journal.h"

#include <lib/fit/function.h>

#include <string>

namespace storage {
namespace fake {

FakeJournal::FakeJournal(FakeJournalDelegate* delegate) : delegate_(delegate) {}

FakeJournal::~FakeJournal() {}

void FakeJournal::Commit(
    fit::function<void(Status, std::unique_ptr<const storage::Commit>)>
        callback) {
  delegate_->Commit(std::move(callback));
}

void FakeJournal::Put(convert::ExtendedStringView key,
                      ObjectIdentifier object_identifier,
                      KeyPriority priority) {
  delegate_->SetValue(key, std::move(object_identifier), priority);
}

void FakeJournal::Delete(convert::ExtendedStringView key) {
  delegate_->Delete(key);
}

void FakeJournal::Clear() { delegate_->Clear(); }

}  // namespace fake
}  // namespace storage
