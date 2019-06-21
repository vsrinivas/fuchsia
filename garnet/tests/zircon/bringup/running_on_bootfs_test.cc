// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdlib.h>
#include <zxtest/zxtest.h>

TEST(RunningOnBootFs, TestRootDirTest) {
  EXPECT_STR_EQ(getenv("TEST_ROOT_DIR"), "/boot");
}
