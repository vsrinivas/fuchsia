// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/symbol-index/reader.h"

#include <sstream>

#include <gtest/gtest.h>

#include "src/lib/fxl/strings/split_string.h"

namespace symbol_index {

namespace {

const char* test_content = R"(
# This is a comment and should be ignored
# Empty lines should also be ignored
   
abc 

# This is an intermediate comment that should also be ignored.
   abcd   efgh
)";

TEST(ReaderTest, Read) {
  Reader reader(' ');
  std::vector<std::vector<std::string>> output;
  std::stringstream file(test_content);

  ASSERT_TRUE(reader.Read(file, "", &output).empty());
  ASSERT_EQ(output.size(), 2UL);
  ASSERT_EQ(output[0].size(), 1UL);
  ASSERT_EQ(output[0][0], "abc");
  ASSERT_EQ(output[1].size(), 2UL);
  ASSERT_EQ(output[1][0], "abcd");
  ASSERT_EQ(output[1][1], "efgh");
}

}  // namespace

}  // namespace symbol_index
