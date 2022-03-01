// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>

#include <gtest/gtest.h>

TEST(Gtest, Pass) { printf("passing test stdout\n"); }

TEST(Gtest, Fail) {
  printf("failing test stdout\n");
  EXPECT_FALSE(true);
}
