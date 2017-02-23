// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gtest/gtest.h"

#include "apps/ledger/src/glue/system/run_in_thread.h"

int main(int argc, char** argv) {
  int test_result;
  auto result = ledger::RunInThread<int>(
      [&argc, &argv]() {
        testing::InitGoogleTest(&argc, argv);
        return RUN_ALL_TESTS();
      },
      &test_result);
  if (result != 0) {
    return result;
  }
  return test_result;
}
