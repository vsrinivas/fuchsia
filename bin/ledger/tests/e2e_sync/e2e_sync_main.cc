// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gtest/gtest.h"
#include "peridot/bin/ledger/tests/integration/integration_test.h"

int main(int argc, char** argv) {
  if (!test::integration::ProcessCommandLine(argc, argv)) {
    return -1;
  }
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
