// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/lib/backoff/testing/test_backoff.h"

namespace ledger {

TestBackoff::TestBackoff() : TestBackoff(kDefaultBackoffDuration) {}

TestBackoff::TestBackoff(zx::duration duration) : backoff_to_return_(duration) {}

TestBackoff::~TestBackoff() {}

zx::duration TestBackoff::GetNext() {
  get_next_count++;
  if (on_get_next_) {
    on_get_next_();
  }
  return backoff_to_return_;
}

void TestBackoff::Reset() { reset_count++; }

void TestBackoff::SetOnGetNext(fit::closure on_get_next) { on_get_next_ = std::move(on_get_next); }

}  // namespace ledger
