// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/testing/test_backoff.h"

#include <lib/fxl/logging.h>

namespace ledger {

TestBackoff::TestBackoff() : get_next_count_(nullptr), duration_(zx::sec(0)) {}

TestBackoff::TestBackoff(int* get_next_count)
    : TestBackoff(get_next_count, zx::sec(0)) {
    }

TestBackoff::TestBackoff(int* get_next_count, zx::duration duration)
    : get_next_count_(get_next_count), duration_(duration) {
      FXL_DCHECK(get_next_count_);
    }

TestBackoff::~TestBackoff() {}

zx::duration TestBackoff::GetNext() {
  if (get_next_count_) {
   (*get_next_count_)++;
  }
  return duration_;
}

void TestBackoff::Reset() {}

}  // namespace ledger
