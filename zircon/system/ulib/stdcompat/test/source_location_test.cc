// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include <lib/stdcompat/source_location.h>

#include <gtest/gtest.h>

namespace {

std::tuple<cpp20::source_location, uint32_t, uint32_t> FooBar() {
  // clang-format off
  uint32_t line = __LINE__ + 1;
  auto source_location = cpp20::source_location::current();
  uint32_t column =      26;
  // clang-format on
  return {source_location, line, column};
}

cpp20::source_location ExampleLoggingFunction(
    cpp20::source_location location = cpp20::source_location::current()) {
  return location;
}

std::tuple<cpp20::source_location, uint32_t, uint32_t> BizBaz() {
  // clang-format off
  uint32_t line = __LINE__ + 1;
  auto source_location = ExampleLoggingFunction();
  uint32_t column =      26;
  // clang-format on
  return {source_location, line, column};
}

TEST(SourceLocationTest, DirectCallValue) {
  auto [location, line, column] = FooBar();
  EXPECT_EQ(location.file_name(), __FILE__);
  EXPECT_EQ(location.function_name(), "FooBar");
  EXPECT_EQ(location.line(), line);
  EXPECT_EQ(location.column(), column);
}

TEST(SourceLocationTest, DefaultParameterValue) {
  auto [location, line, column] = BizBaz();
  EXPECT_EQ(location.file_name(), __FILE__);
  EXPECT_EQ(location.function_name(), "BizBaz");
  EXPECT_EQ(location.line(), line);
  EXPECT_EQ(location.column(), column);
}

#if __cpp_lib_source_location >= 201907L && !defined(LIB_STDCOMPAT_USE_POLYFILLS)
TEST(SourceLocationTest, IsAliasOfStdInStd20) {
  static_assert(std::is_same_v<cpp20::source_location, std::source_location>);
}
#endif  // __cpp_lib_source_location >= 201907L && !defined(LIB_STDCOMPAT_USE_POLYFILLS)

}  // namespace
