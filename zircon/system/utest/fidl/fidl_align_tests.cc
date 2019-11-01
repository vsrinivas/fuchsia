// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/fidl.h>

#include <unittest/unittest.h>

namespace {

bool fidl_align() {
  BEGIN_TEST;

  EXPECT_EQ(8, FIDL_ALIGN(1));
  EXPECT_EQ(8, FIDL_ALIGN(8));
  EXPECT_EQ(16, FIDL_ALIGN(9));
  EXPECT_EQ(16, FIDL_ALIGN(16));
  EXPECT_EQ(24, FIDL_ALIGN(17));
  EXPECT_EQ(24, FIDL_ALIGN(24));

  END_TEST;
}

}  // namespace

BEGIN_TEST_CASE(fidl_align_tests)
RUN_TEST(fidl_align)
END_TEST_CASE(fidl_align_tests)
