// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

#include "guest_test.h"

static constexpr size_t kVirtioConsoleMessageCount = 100;

template <class T>
using VirtioConsoleGuestTest = GuestTest<T>;

TYPED_TEST_SUITE(VirtioConsoleGuestTest, AllGuestTypes);

TYPED_TEST(VirtioConsoleGuestTest, VirtioConsole) {
  // Test many small packets.
  std::string result;
  for (size_t i = 0; i != kVirtioConsoleMessageCount; ++i) {
    EXPECT_EQ(this->Execute({"echo", "test"}, &result), ZX_OK);
    EXPECT_EQ(result, "test\n");
  }

  // Test large packets. Note that we must keep the total length below 4096,
  // which is the maximum line length for dash.
  std::string test_data = "";
  for (size_t i = 0; i != kVirtioConsoleMessageCount; ++i) {
    test_data.append("Lorem ipsum dolor sit amet consectetur");
  }
  EXPECT_EQ(this->Execute({"echo", test_data.c_str()}, &result), ZX_OK);
  test_data.append("\n");
  EXPECT_EQ(result, test_data);
}
