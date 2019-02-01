// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/iquery/utils.h"

#include "gtest/gtest.h"

namespace {

TEST(IqueryUtils, IsStringPrintable) {
  EXPECT_TRUE(iquery::IsStringPrintable("hello"));
  EXPECT_TRUE(iquery::IsStringPrintable("hello world"));
  EXPECT_TRUE(iquery::IsStringPrintable("hello\tworld"));
  EXPECT_TRUE(iquery::IsStringPrintable("hello\nworld\r\nagain"));
  // <smiling face> :-) <filled black star>.
  // \x05 is not printable ASCII, so this checks that we properly increment the
  // index.
  EXPECT_TRUE(iquery::IsStringPrintable("\u263A :-) \u2605"));
  EXPECT_FALSE(iquery::IsStringPrintable("hello\x6"));
  EXPECT_FALSE(iquery::IsStringPrintable("hello\x80"));
  EXPECT_FALSE(iquery::IsStringPrintable(std::string("hello\0", 6)));
}

}  // namespace
