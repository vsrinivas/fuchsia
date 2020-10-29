// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>

#include <gtest/gtest.h>

char **g_argv;
int g_argc;
int main(int argc, char **argv) {
  testing::InitGoogleTest(&argc, argv);
  g_argv = argv;
  g_argc = argc;
  return RUN_ALL_TESTS();
}

TEST(TestArg, TestArg) {
  EXPECT_EQ(g_argc, 3);
  EXPECT_STREQ(g_argv[1], "--my_custom_arg");
  EXPECT_STREQ(g_argv[2], "--my_custom_arg2");
}
