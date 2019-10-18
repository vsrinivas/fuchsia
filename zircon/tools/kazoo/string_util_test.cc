// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/kazoo/string_util.h"

#include "tools/kazoo/test.h"

namespace {

TEST(String, StartsWith) {
  EXPECT_TRUE(StartsWith("", ""));
  EXPECT_TRUE(StartsWith("a", "a"));
  EXPECT_TRUE(StartsWith("Stuff", "Stuff"));
  EXPECT_TRUE(StartsWith("Stuffa", "Stuff"));
  EXPECT_TRUE(StartsWith("Stuffa", ""));
  EXPECT_FALSE(StartsWith("Stuffa", "tuffa"));
  EXPECT_FALSE(StartsWith("Stuff", "f"));
  EXPECT_FALSE(StartsWith("a", "aaaaaaaaaaaa"));
}

}  // namespace
