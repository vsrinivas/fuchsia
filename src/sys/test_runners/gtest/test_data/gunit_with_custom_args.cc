// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>

#include <gtest/gtest.h>

char **g_argv;
int g_argc;
int main(int argc, char **argv) {
  const std::string gunit_flag_start = "--gunit_";
  const std::string gtest_flag = "--gtest";

  for (int i = 0; i < argc; i++) {
    if (strncmp(gunit_flag_start.c_str(), argv[i], gunit_flag_start.length()) == 0) {
      strncpy(argv[i], gtest_flag.c_str(), gtest_flag.length());
    } else if (strncmp(gtest_flag.c_str(), argv[i], gtest_flag.length()) == 0) {
      printf("got gtest flag in gunit simulated test: %s\n", argv[i]);
      exit(1);
    }
  }

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
