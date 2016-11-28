// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/app/merging/dummy_merger.h"

#include "apps/ledger/src/callback/cancellable_helper.h"

namespace ledger {
DummyMerger::DummyMerger() {}

DummyMerger::~DummyMerger() {}

ftl::RefPtr<callback::Cancellable> DummyMerger::Merge(
    std::unique_ptr<const storage::Commit> head_1,
    std::unique_ptr<const storage::Commit> head_2,
    std::unique_ptr<const storage::Commit> ancestor) {
  return callback::CreateDoneCancellable();
}

}  // namespace ledger
