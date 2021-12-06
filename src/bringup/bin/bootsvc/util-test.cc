// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/bringup/bin/bootsvc/util.h"

#include <zircon/types.h>

#include <algorithm>
#include <array>
#include <string>
#include <string_view>
#include <vector>

#include <zxtest/zxtest.h>

namespace {

struct SplitStringTestCase {
  const char* input;
  char delimiter;
};

// Test the SplitString util individually.
TEST(UtilTest, TestSplitString) {
  struct TestCase {
    std::string_view input;
    char delimiter;
    std::vector<std::string> expected;
  };

  // Makeshift parametrized test.
  const std::array cases = {
      TestCase{.input = "", .delimiter = ',', .expected = {}},
      TestCase{.input = "abcd", .delimiter = ',', .expected = {"abcd"}},
      TestCase{.input = "a,b c,d", .delimiter = ',', .expected = {"a", "b c", "d"}},
      TestCase{.input = "a,b c,d", .delimiter = ' ', .expected = {"a,b", "c,d"}},
      TestCase{.input = "::a:", .delimiter = ':', .expected = {"", "", "a", ""}},
  };

  for (auto test_case : cases) {
    std::string_view input = test_case.input;
    const auto& expected = test_case.expected;

    auto actual = bootsvc::SplitString(input, test_case.delimiter);
    EXPECT_EQ(actual.size(), expected.size(), "Input: %.*s", static_cast<int>(input.size()),
              input.data());
    for (size_t i = 0; i < std::min(actual.size(), expected.size()); ++i) {
      EXPECT_STR_EQ(actual[i], expected[i], "Input: %.*s", static_cast<int>(input.size()),
                    input.data());
    }
  }
}

// Make sure that we can parse boot args from a configuration string
TEST(UtilTest, TestParseBootArgs) {
  const char config1[] = R"(
# comment
key
key=value
=value
)";

  // Parse a valid config.
  std::vector<char> buf;
  zx_status_t status = bootsvc::ParseBootArgs(config1, &buf);
  ASSERT_EQ(ZX_OK, status);

  const char expected[] = "key\0key=value";
  auto actual = reinterpret_cast<const uint8_t*>(buf.data());
  ASSERT_BYTES_EQ(reinterpret_cast<const uint8_t*>(expected), actual, buf.size(), "");

  // Parse a config that doesn't ends with newline.
  const char config2[] = "key=value";
  status = bootsvc::ParseBootArgs(config2, &buf);
  ASSERT_EQ(ZX_OK, status);

  // Parse an invalid config.
  const char config3[] = "k ey=value";
  status = bootsvc::ParseBootArgs(config3, &buf);
  ASSERT_EQ(ZX_ERR_INVALID_ARGS, status);
}

}  // namespace
