// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/backoff/testing/test_backoff.h"

namespace backoff {

TestBackoff::TestBackoff() {}

TestBackoff::~TestBackoff() {}

zx::duration TestBackoff::GetNext() {
  get_next_count++;
  if (on_get_next_) {
    on_get_next_();
  }
  return backoff_to_return;
}

void TestBackoff::Reset() { reset_count++; }

void TestBackoff::SetOnGetNext(fxl::Closure on_get_next) {
  on_get_next_ = on_get_next;
}

}  // namespace backoff
