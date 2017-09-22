// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/backoff/test/test_backoff.h"

namespace backoff {

namespace test {

TestBackoff::TestBackoff() {}

TestBackoff::~TestBackoff() {}

fxl::TimeDelta TestBackoff::GetNext() {
  get_next_count++;
  if (on_get_next_) {
    on_get_next_();
  }
  return backoff_to_return;
}

void TestBackoff::Reset() {
  reset_count++;
}

void TestBackoff::SetOnGetNext(fxl::Closure on_get_next) {
  on_get_next_ = on_get_next;
}

}  // namespace test

}  // namespace backoff
