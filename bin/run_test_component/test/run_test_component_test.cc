// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <sys/stat.h>

#include "gtest/gtest.h"

TEST(Run, Test) {
  struct stat s;
  EXPECT_NE(stat("/system", &s), 0)
      << "system should not be present when this is executed as component";
}
