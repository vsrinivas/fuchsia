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
#include <lib/zx/eventpair.h>

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

bool IsPeerValid(const zx::unowned_eventpair& handle) {
  zx_signals_t observed_signals = {};
  switch (handle->wait_one(ZX_EVENTPAIR_PEER_CLOSED,
                           zx::deadline_after(zx::msec(0)),
                           &observed_signals)) {
  case ZX_ERR_TIMED_OUT:
    // timeout implies peer-closed was not observed
    return true;
  case ZX_OK:
    return (observed_signals & ZX_EVENTPAIR_PEER_CLOSED) == 0;
  default:
    return false;
  }
}

TEST(MoveUnion, NoDoubleDestructPayload) {
  using namespace fidl::llcpp::types::test;
  zx::eventpair canary_a, canary_b;
  ASSERT_EQ(zx::eventpair::create(0, &canary_a, &canary_b), ZX_OK);
  ASSERT_TRUE(IsPeerValid(zx::unowned_eventpair(canary_a)));
  // Initialize the union such that the handle |h| within the |MoveOnlyStruct|
  // payload overlaps with the eventpair.
  zx_handle_t h = canary_b.release();
  static_assert(sizeof(TestUnion) == 8);
  uint8_t dangerous_buffer[sizeof(TestUnion)] = {};
  memcpy(&dangerous_buffer[4], &h, sizeof(h));
  {
    TestUnion* test_union = reinterpret_cast<TestUnion*>(dangerous_buffer);
    TestUnion union_with_absent_handle;
    union_with_absent_handle.set_move_only(MoveOnlyStruct {
      .h = zx::handle()
    });
    // Manually running the move constructor.
    new (test_union) TestUnion(std::move(union_with_absent_handle));
  }
  // |canary_b| should not be closed.
  EXPECT_TRUE(IsPeerValid(zx::unowned_eventpair(canary_a)));
  zx_handle_close(h);
}
