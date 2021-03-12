// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>

#include <gtest/gtest.h>

TEST(SampleTest1, SimpleFail) { EXPECT_FALSE(true); }

TEST(SampleTest1, Crashing) { abort(); }

TEST(SampleTest2, SimplePass) {}

class SampleFixture : public ::testing::Test {};

TEST_F(SampleFixture, Test1) {}

TEST_F(SampleFixture, Test2) {}

TEST(SampleDisabled, DISABLED_TestPass) {}

TEST(SampleDisabled, DISABLED_TestFail) { EXPECT_FALSE(true); }

TEST(SampleDisabled, DynamicSkip) { GTEST_SKIP(); }

class SampleParameterizedTestFixture : public ::testing::TestWithParam<int> {};

TEST_P(SampleParameterizedTestFixture, Test) {}

INSTANTIATE_TEST_SUITE_P(Tests, SampleParameterizedTestFixture,
                         ::testing::Values(1, 711, 1989, 2013));

TEST(WriteToStdout, TestPass) {
  printf("first msg\n");
  printf("second msg\n\n\n");
  printf("third msg\n\n");
}

TEST(WriteToStdout, TestFail) {
  printf("first msg\n");
  EXPECT_FALSE(true);
  printf("second msg\n");
}
