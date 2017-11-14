// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gtest/gtest.h"
#include "lib/fidl/cpp/bindings/tests/util/test_waiter.h"

int main(int argc, char** argv) {
  fidl::test::InitAsyncWaiter();
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
