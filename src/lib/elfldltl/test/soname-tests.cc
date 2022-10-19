// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/elfldltl/gnu-hash.h>
#include <lib/elfldltl/soname.h>

#include <optional>
#include <string>
#include <vector>

#include <zxtest/zxtest.h>

#include "tests.h"

namespace {

TEST(ElfldltlSonameTests, Basic) {
  elfldltl::Soname name{"test"};
  EXPECT_STREQ(name.str(), "test");
  elfldltl::Soname other{"other"};
  EXPECT_STREQ(other.str(), "other");
  name = other;
  EXPECT_STREQ(name.str(), "other");
  EXPECT_EQ(other, name);
  EXPECT_EQ(other.hash(), elfldltl::GnuHashString("other"));

  elfldltl::Soname a{"a"}, b{"b"};
  EXPECT_LT(a, b);
  EXPECT_LE(a, b);
  EXPECT_LE(a, a);
  EXPECT_GT(b, a);
  EXPECT_GE(b, a);
  EXPECT_GE(a, a);
  EXPECT_EQ(a, a);
  EXPECT_NE(a, b);
}

}  // anonymous namespace
