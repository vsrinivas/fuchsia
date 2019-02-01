// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/common/regex.h"

#include <gtest/gtest.h>

namespace zxdb {

TEST(Regex, CaseInsensitive) {
  Regex regex;
  // Inser
  Err err = regex.Init("test");
  EXPECT_FALSE(err.has_error()) << err.msg();

  // Init again should fail.
  err = regex.Init("test");
  EXPECT_TRUE(err.has_error());

  EXPECT_TRUE(regex.Match("test"));
  EXPECT_FALSE(regex.Match("bla"));
  EXPECT_TRUE(regex.Match("aaaaTESTaaaa"));
}

TEST(Regex, CaseSensitive) {
  Regex regex;
  Err err = regex.Init("TEST.*test", Regex::CompareType::kCaseSensitive);
  EXPECT_FALSE(err.has_error()) << err.msg();

  // Init again should fail.
  err = regex.Init("test");
  EXPECT_TRUE(err.has_error());

  EXPECT_FALSE(regex.Match("test"));
  EXPECT_FALSE(regex.Match("TEST"));
  EXPECT_FALSE(regex.Match("TESTaaaTEST"));
  EXPECT_TRUE(regex.Match("TESTaaatest"));
}

}  // namespace zxdb
