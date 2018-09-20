// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gtest/gtest.h"
#include "internal_list_fuzzer_helpers.h"

namespace overnet {
namespace internal_list {

TEST(InternalList, Basic) {
  Fuzzer f;
  f.Verify();
  EXPECT_TRUE(f.PushBack(1, 1));
  f.Verify();
  EXPECT_TRUE(f.PushBack(2, 1));
  f.Verify();
  EXPECT_TRUE(f.PushBack(3, 1));
  f.Verify();
  EXPECT_TRUE(f.PushFront(7, 1));
  f.Verify();
  EXPECT_TRUE(f.PushFront(8, 1));
  f.Verify();
  EXPECT_TRUE(f.PushFront(9, 1));
  f.Verify();
  EXPECT_TRUE(f.Remove(2, 1));
  f.Verify();
  EXPECT_TRUE(f.Remove(7, 1));
  f.Verify();
  EXPECT_TRUE(f.Remove(9, 1));
  f.Verify();
  EXPECT_TRUE(f.Remove(1, 1));
  f.Verify();

  EXPECT_TRUE(f.PushBack(1, 2));
  f.Verify();
  EXPECT_TRUE(f.PushBack(2, 2));
  f.Verify();
  EXPECT_TRUE(f.PushBack(3, 2));
  f.Verify();
  EXPECT_TRUE(f.PushFront(7, 2));
  f.Verify();
  EXPECT_TRUE(f.PushFront(8, 2));
  f.Verify();
  EXPECT_TRUE(f.PushFront(9, 2));
  f.Verify();
  EXPECT_TRUE(f.Remove(2, 2));
  f.Verify();
  EXPECT_TRUE(f.Remove(7, 2));
  f.Verify();
  EXPECT_TRUE(f.Remove(9, 2));
  f.Verify();
  EXPECT_TRUE(f.Remove(1, 2));
  f.Verify();
}

}  // namespace internal_list
}  // namespace overnet
