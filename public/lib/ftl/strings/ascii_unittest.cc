// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gtest/gtest.h"
#include "lib/ftl/strings/ascii.h"
#include "lib/ftl/strings/string_view.h"

namespace ftl {
namespace {

TEST(StringUtil, ToLowerASCII) {
  EXPECT_EQ(',', ftl::ToLowerASCII(','));
  EXPECT_EQ('a', ftl::ToLowerASCII('a'));
  EXPECT_EQ('a', ftl::ToLowerASCII('A'));
}

TEST(StringUtil, ToUpperASCII) {
  EXPECT_EQ(',', ftl::ToUpperASCII(','));
  EXPECT_EQ('A', ftl::ToUpperASCII('a'));
  EXPECT_EQ('A', ftl::ToUpperASCII('A'));
}

TEST(StringUtil, EqualsCaseInsensitiveASCII) {
  EXPECT_TRUE(EqualsCaseInsensitiveASCII("", ""));
  EXPECT_TRUE(EqualsCaseInsensitiveASCII("abcd", "abcd"));
  EXPECT_TRUE(EqualsCaseInsensitiveASCII("abcd", "aBcD"));
  EXPECT_TRUE(EqualsCaseInsensitiveASCII("abcd", "ABCD"));

  EXPECT_FALSE(EqualsCaseInsensitiveASCII("abcd", ""));
  EXPECT_FALSE(EqualsCaseInsensitiveASCII("abcd", "abc"));
  EXPECT_FALSE(EqualsCaseInsensitiveASCII("abcd", "ABC"));
  EXPECT_FALSE(EqualsCaseInsensitiveASCII("abcd", "ABCDE"));
}

}  // namespace
}  // namespace ftl
