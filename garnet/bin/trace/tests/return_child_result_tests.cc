// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

#include "garnet/bin/trace/tests/integration_test_utils.h"
#include "garnet/bin/trace/tests/run_test.h"
#include "src/developer/tracing/lib/test_utils/run_program.h"

namespace tracing {
namespace test {

namespace {

const char kChildPath[] = "/pkg/bin/return_1234";

constexpr const int kChildReturnCode = 1234;

TEST(ReturnChildResult, False) {
  zx::job job{};  // -> default job
  zx::process child;
  std::vector<std::string> args{
    "record",
    "--return-child-result=false",
    "--spawn",
    kChildPath};
  ASSERT_TRUE(RunTrace(job, args, &child));

  int64_t return_code;
  ASSERT_TRUE(WaitAndGetReturnCode("trace", child, &return_code));
  EXPECT_EQ(return_code, 0);
}

TEST(ReturnChildResult, True) {
  zx::job job{};  // -> default job
  zx::process child;
  std::vector<std::string> args{
    "record",
    "--return-child-result=true",
    "--spawn",
    kChildPath};
  ASSERT_TRUE(RunTrace(job, args, &child));

  int64_t return_code;
  ASSERT_TRUE(WaitAndGetReturnCode("trace", child, &return_code));
  EXPECT_EQ(return_code, kChildReturnCode);
}

}  // namespace

}  // namespace test
}  // namespace tracing
