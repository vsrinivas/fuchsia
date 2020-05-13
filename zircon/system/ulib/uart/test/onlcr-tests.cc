// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/uart/chars-from.h>

#include <array>
#include <string>
#include <string_view>

#include <zxtest/zxtest.h>

using namespace std::literals;

namespace {

template <typename T>
std::string StringFrom(const T& from) {
  return {std::begin(from), std::end(from)};
}

TEST(CharsFromTests, Onlcr) {
  // Works on string constants (char[N]).
  EXPECT_STR_EQ(StringFrom(uart::CharsFrom("hello\n")), "hello\r\n");

  // Works on std::string_view.
  EXPECT_STR_EQ(StringFrom(uart::CharsFrom("foo\nbar"sv)), "foo\r\nbar");

  // Works on std::string.
  EXPECT_STR_EQ(StringFrom(uart::CharsFrom("\nbye\n"s)), "\r\nbye\r\n");
}

}  // namespace
