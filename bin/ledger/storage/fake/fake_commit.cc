// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/storage/fake/fake_commit.h"

#include <memory>
#include <string>

#include "peridot/bin/ledger/storage/fake/fake_journal_delegate.h"
#include "peridot/bin/ledger/storage/public/commit.h"
#include "peridot/bin/ledger/storage/public/iterator.h"

namespace storage {
namespace fake {

FakeCommit::FakeCommit(FakeJournalDelegate* journal) : journal_(journal) {}
FakeCommit::~FakeCommit() {}

std::unique_ptr<Commit> FakeCommit::Clone() const {
  return std::make_unique<FakeCommit>(journal_);
}

const CommitId& FakeCommit::GetId() const {
  return journal_->GetId();
}

std::vector<CommitIdView> FakeCommit::GetParentIds() const {
  return {journal_->GetParentId()};
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

fxl::StringView FakeCommit::GetStorageBytes() const {
  return fxl::StringView();
}

}  // namespace fake
}  // namespace storage
