// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include <fidl/llcpp/types/test/llcpp/fidl.h>
#include "gtest/gtest.h"
#include <lib/zx/event.h>

TEST(UnionPayload, Primitive) {
  using namespace fidl::llcpp::types::test;
  TestUnion test_union;
  EXPECT_EQ(TestUnion::Tag::Invalid, test_union.which());
  test_union.set_primitive(5);
  EXPECT_EQ(TestUnion::Tag::kPrimitive, test_union.which());
}

TEST(UnionPayload, CopyableStruct) {
  using namespace fidl::llcpp::types::test;
  {
    TestUnion test_union;
    EXPECT_EQ(TestUnion::Tag::Invalid, test_union.which());
    test_union.set_copyable(CopyableStruct { .x = 5 });
    EXPECT_EQ(TestUnion::Tag::kCopyable, test_union.which());
  }
  {
    TestUnion test_union;
    EXPECT_EQ(TestUnion::Tag::Invalid, test_union.which());
    CopyableStruct copyable_struct { .x = 5 };
    test_union.set_copyable(copyable_struct);
    EXPECT_EQ(TestUnion::Tag::kCopyable, test_union.which());
  }
}

TEST(UnionPayload, MoveOnlyStruct) {
  // Move-only types are only settable as rvalue reference.
  using namespace fidl::llcpp::types::test;
  {
    TestUnion test_union;
    EXPECT_EQ(TestUnion::Tag::Invalid, test_union.which());
    test_union.set_move_only(MoveOnlyStruct { .h = zx::handle() });
    EXPECT_EQ(TestUnion::Tag::kMoveOnly, test_union.which());
  }
  {
    TestUnion test_union;
    EXPECT_EQ(TestUnion::Tag::Invalid, test_union.which());
    zx::event event;
    ASSERT_EQ(ZX_OK, zx::event::create(0, &event));
    MoveOnlyStruct move_only_struct { .h = std::move(event) };
    EXPECT_NE(ZX_HANDLE_INVALID, move_only_struct.h.get());
    test_union.set_move_only(std::move(move_only_struct));
    EXPECT_EQ(TestUnion::Tag::kMoveOnly, test_union.which());
    EXPECT_EQ(ZX_HANDLE_INVALID, move_only_struct.h.get());
  }
}

TEST(MoveUnion, Primitive) {
  using namespace fidl::llcpp::types::test;
  TestUnion test_union_a;
  TestUnion test_union_b;
  test_union_a.set_primitive(5);
  test_union_b = std::move(test_union_a);
  EXPECT_EQ(TestUnion::Tag::Invalid, test_union_a.which());
  EXPECT_EQ(TestUnion::Tag::kPrimitive, test_union_b.which());
  EXPECT_EQ(5, test_union_b.primitive());
}

TEST(MoveUnion, CopyableStruct) {
  using namespace fidl::llcpp::types::test;
  TestUnion test_union_a;
  TestUnion test_union_b;
  test_union_a.set_copyable(CopyableStruct { .x = 5 });
  test_union_b = std::move(test_union_a);
  EXPECT_EQ(TestUnion::Tag::Invalid, test_union_a.which());
  EXPECT_EQ(TestUnion::Tag::kCopyable, test_union_b.which());
  EXPECT_EQ(5, test_union_b.copyable().x);
}

TEST(MoveUnion, MoveOnlyStruct) {
  using namespace fidl::llcpp::types::test;
  TestUnion test_union_a;
  TestUnion test_union_b;
  zx::event event;
  ASSERT_EQ(ZX_OK, zx::event::create(0, &event));
  MoveOnlyStruct move_only_struct { .h = std::move(event) };
  test_union_a.set_move_only(std::move(move_only_struct));
  test_union_b = std::move(test_union_a);
  EXPECT_EQ(TestUnion::Tag::Invalid, test_union_a.which());
  EXPECT_EQ(TestUnion::Tag::kMoveOnly, test_union_b.which());
  EXPECT_NE(ZX_HANDLE_INVALID, test_union_b.move_only().h);
}
