// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/cpp/box.h>

#include <gtest/gtest.h>

namespace {

TEST(Box, DefaultConstruction) {
  fidl::Box<int> box;
  EXPECT_FALSE(box);
}

TEST(Box, MakeUnique) {
  fidl::Box<int> box;
  box = std::make_unique<int>(42);
  EXPECT_TRUE(box);
  EXPECT_EQ(*box, 42);
}

TEST(Box, Move) {
  fidl::Box<int> box1;
  EXPECT_FALSE(box1);
  fidl::Box<int> box2 = std::make_unique<int>(42);
  EXPECT_TRUE(box2);
  box1 = std::move(box2);
  EXPECT_FALSE(box2);
  EXPECT_TRUE(box1);
  EXPECT_EQ(*box1, 42);
}

TEST(Box, ConvertToUniquePtr) {
  fidl::Box<int> box(new int(42));
  const std::unique_ptr<int>& ref = box.unique_ptr();
  EXPECT_EQ(*ref, 42);
  std::unique_ptr<int>& mut_ref = box.unique_ptr();
  EXPECT_EQ(*mut_ref, 42);
  std::unique_ptr<int> owned = std::move(box.unique_ptr());
  EXPECT_EQ(*owned, 42);
  EXPECT_FALSE(box);
}

TEST(Box, UniquePtrInterface) {
  fidl::Box<int> box;
  EXPECT_FALSE(box);
  EXPECT_EQ(nullptr, box.operator->());
  box.reset();
}

TEST(Box, Equality) {
  fidl::Box<int> box1(new int(42));
  fidl::Box<int> box2(new int(42));
  fidl::Box<int> different(new int(100));
  fidl::Box<int> empty(nullptr);
  EXPECT_EQ(box1, box2);
  EXPECT_NE(box1, nullptr);
  EXPECT_NE(box1, empty);
  EXPECT_NE(box1, different);
  EXPECT_EQ(empty, empty);
}

}  // namespace
