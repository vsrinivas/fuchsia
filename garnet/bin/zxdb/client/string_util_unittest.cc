// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/common/string_util.h"
#include "gtest/gtest.h"

namespace zxdb {

TEST(StringUtil, StringEndsWith) {
  EXPECT_FALSE(StringEndsWith("short", "much too long"));
  EXPECT_FALSE(StringEndsWith("a", "b"));
  EXPECT_FALSE(StringEndsWith("aaa", "b"));
  EXPECT_TRUE(StringEndsWith("aaab", "b"));
  EXPECT_TRUE(StringEndsWith("aaabcde", "bcde"));
  EXPECT_TRUE(StringEndsWith("bcde", "bcde"));
  EXPECT_TRUE(StringEndsWith("aaab", ""));
}

}  // namespace zxdb
