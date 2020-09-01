// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#define FORCE_FIT_SOURCE_LOCATION
#include <lib/fit/source_location.h>

#include <zxtest/zxtest.h>

namespace {

std::tuple<fit::source_location, uint32_t, uint32_t> FooBar() {
  // clang-format off
  uint32_t line = __LINE__ + 1;
  auto source_location = fit::source_location::current();
  uint32_t column =      26;
  // clang-format on
  return {source_location, line, column};
}

fit::source_location ExampleLoggingFunction(
    fit::source_location location = fit::source_location::current()) {
  return location;
}

std::tuple<fit::source_location, uint32_t, uint32_t> BizBaz() {
  // clang-format off
  uint32_t line = __LINE__ + 1;
  auto source_location = ExampleLoggingFunction();
  uint32_t column =      26;
  // clang-format on
  return {source_location, line, column};
}

TEST(SourceLocationTests, direct_call_values) {
  auto [location, line, column] = FooBar();
  EXPECT_EQ(location.file_name(), __FILE__);
  EXPECT_EQ(location.function_name(), "FooBar");
  EXPECT_EQ(location.line(), line);
  EXPECT_EQ(location.column(), column);
}

TEST(SourceLocationTests, default_parameter_values) {
  auto [location, line, column] = BizBaz();
  EXPECT_EQ(location.file_name(), __FILE__);
  EXPECT_EQ(location.function_name(), "BizBaz");
  EXPECT_EQ(location.line(), line);
  EXPECT_EQ(location.column(), column);
}

}  // namespace
