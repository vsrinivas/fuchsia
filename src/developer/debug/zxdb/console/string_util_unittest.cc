// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/console/string_util.h"
#include "gtest/gtest.h"

namespace zxdb {

TEST(StringUtil, UnicodeCharWidth) {
  // Valid ASCII.
  EXPECT_EQ(0u, UnicodeCharWidth(std::string()));
  EXPECT_EQ(5u, UnicodeCharWidth("hello"));

  // Valid UTF-8.
  EXPECT_EQ(6u, UnicodeCharWidth("\xe2\x96\xb6hello"));

  // Embedded nulls count as one.
  std::string with_null;
  with_null.push_back('a');
  with_null.push_back(0);
  with_null.push_back('b');
  EXPECT_EQ(3u, UnicodeCharWidth(with_null));

  // Invalid UTF-8, each possible invalid sequence counts as one. (This is more
  // about defining specific behavior, we don't actually know what the console
  // will do with this input.)
  EXPECT_EQ(3u, UnicodeCharWidth("Hi\xe2\x96"));
  EXPECT_EQ(3u, UnicodeCharWidth("\xe2\x96hi"));
}

}  // namespace zxdb
