// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/gtest/real_loop_fixture.h>

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  int result = RUN_ALL_TESTS();

  return result;
}
