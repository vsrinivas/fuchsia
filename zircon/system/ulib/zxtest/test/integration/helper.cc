// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "helper.h"

#include <fbl/vector.h>

namespace zxtest {
namespace test {
namespace {

fbl::Vector<void (*)()>* GetCheckFns() {
  static fbl::Vector<void (*)()> check_fns;
  return &check_fns;
}

}  // namespace

void AddCheckFunction(void (*check)()) {
  GetCheckFns()->push_back(check);
}

void CheckAll() {
  for (auto* fn : *GetCheckFns()) {
    fn();
  }
}

}  // namespace test
}  // namespace zxtest

void zxtest_add_check_function(void (*check)(void)) {
  zxtest::test::AddCheckFunction(check);
}

void verify_expectation(test_expectation_t* expectation) {
  if (expectation->expect_errors) {
    CHECK_ERROR();
  } else {
    CHECK_NO_ERROR();
  }
  if (expectation->checkpoint_reached != expectation->checkpoint_reached_expected) {
    fprintf(stdout, "[%s:%zu]: Failed due to %s\n", expectation->filename, expectation->line,
            expectation->reason);
    ZX_ASSERT_MSG(expectation->checkpoint_reached_expected == expectation->checkpoint_reached,
                  "Checkpoint expectation failed. See error above.");
  }
}
