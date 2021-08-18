// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/syslog/cpp/macros.h>
#include <lib/zx/time.h>
#include <stdio.h>
#include <zircon/syscalls.h>
#include <zircon/time.h>

#include <gtest/gtest.h>

TEST(SampleTest1, SimpleFail) { EXPECT_FALSE(true); }

TEST(SampleTest2, SimplePass) {}

TEST(SampleTest2, SimpleLog) {
  FX_LOGS(INFO) << "info msg";
  FX_LOGS(WARNING) << "warn msg";

  // TODO(fxbug.dev/79121): Component manager may send the Stop event to Archivist before it
  // sends CapabilityRequested. In this case the logs may be lost. This sleep delays
  // terminating the test to give component manager time to send the CapabilityRequested
  // event. This sleep should be removed once component manager orders the events.
  ASSERT_EQ(zx_nanosleep(zx::deadline_after(zx::sec(2)).get()), ZX_OK);
}

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
  printf("TestPass - first msg\n");
  printf("TestPass - second msg\n\n\n");
  printf("TestPass - third msg\n\n");
}

TEST(WriteToStdout, TestFail) {
  printf("TestPass - first msg\n");
  EXPECT_FALSE(true);
  printf("TestPass - second msg\n");
}
