// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

#include "guest_test.h"

template <class T>
using CoreGuestTest = GuestTest<T>;

TYPED_TEST_SUITE(CoreGuestTest, AllGuestTypes);

TYPED_TEST(CoreGuestTest, LaunchGuest) {
  std::string result;
  EXPECT_EQ(this->Execute({"echo", "test"}, &result), ZX_OK);
  EXPECT_EQ(result, "test\n");
}
