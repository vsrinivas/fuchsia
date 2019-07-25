// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

#include "guest_test.h"

using testing::HasSubstr;

static constexpr char kVirtioRngUtil[] = "virtio_rng_test_util";

template <class T>
using VirtioRngGuestTest = GuestTest<T>;

TYPED_TEST_SUITE(VirtioRngGuestTest, AllGuestTypes);

TYPED_TEST(VirtioRngGuestTest, VirtioRng) {
  std::string result;
  EXPECT_EQ(this->RunUtil(kVirtioRngUtil, {}, &result), ZX_OK);
  EXPECT_THAT(result, HasSubstr("PASS"));
}
