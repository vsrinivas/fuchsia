// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdint.h>

#include <zxtest/zxtest.h>

// Sanity tests that enforce compile time check for printing primitive types, and preventing
// undefined symbols.
TEST(CPrintTest, Uint32) {
  uint32_t a = 0;

  ASSERT_EQ(a, 0u);
}

TEST(CPrintTest, Int32) {
  int32_t a = 0;

  ASSERT_EQ(a, 0);
}

TEST(CPrintTest, Uint64) {
  int64_t a = 0;

  ASSERT_EQ(a, 0u);
}

TEST(CPrintTest, Int64) {
  int64_t a = 0;

  ASSERT_EQ(a, 0);
}

TEST(CPrintTest, Str) {
  const char* a = "MyStr";

  ASSERT_STR_EQ(a, "MyStr");
}
