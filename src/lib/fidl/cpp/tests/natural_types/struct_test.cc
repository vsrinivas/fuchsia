// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/test.types/cpp/natural_types.h>

#include <gtest/gtest.h>

namespace {

zx::event MakeEvent() {
  zx::event event;
  zx_status_t status = zx::event::create(0, &event);
  ZX_ASSERT(status == ZX_OK);
  return event;
}

TEST(Struct, DefaultConstruction) {
  test_types::CopyableStruct s;
  EXPECT_EQ(s.x(), 0);

  test_types::MoveOnlyStruct m;
  EXPECT_FALSE(m.h().is_valid());
}

TEST(Struct, InitializationCopyable) {
  test_types::CopyableStruct cs{42};
  EXPECT_EQ(cs.x(), 42);

  test_types::VectorStruct vs{std::vector<uint32_t>{1, 2, 3}};
  EXPECT_EQ(vs.v().size(), 3UL);

  // Values should be copied when passed into constructors.
  std::vector<uint32_t> v{1, 2, 3};
  EXPECT_EQ(v.size(), 3UL);
  test_types::VectorStruct vs2{v};
  EXPECT_EQ(vs2.v().size(), 3UL);
  EXPECT_EQ(v.size(), 3UL);

  // Modifying this vector shouldn't modify the vector in the table.
  v.push_back(4);
  EXPECT_EQ(v.size(), 4UL);
  EXPECT_EQ(vs2.v().size(), 3UL);
}

TEST(Struct, AggregateInitializationCopyable) {
  test_types::CopyableStruct cs{{.x = 42}};
  EXPECT_EQ(cs.x(), 42);

  test_types::VectorStruct vs{{.v = std::vector<uint32_t>{1, 2, 3}}};
  EXPECT_EQ(vs.v().size(), 3UL);

  // Values should be copied when passed into constructors.
  std::vector<uint32_t> v{1, 2, 3};
  EXPECT_EQ(v.size(), 3UL);
  test_types::VectorStruct vs2{{.v = v}};
  EXPECT_EQ(vs2.v().size(), 3UL);
  EXPECT_EQ(v.size(), 3UL);

  // Modifying this vector shouldn't modify the vector in the table.
  v.push_back(4);
  EXPECT_EQ(v.size(), 4UL);
  EXPECT_EQ(vs2.v().size(), 3UL);
}

TEST(Struct, InitializationMoveOnly) {
  zx::event event = MakeEvent();
  ASSERT_TRUE(event.is_valid());
  zx_handle_t handle = event.get();

  test_types::HandleStruct hs{std::move(event)};
  EXPECT_TRUE(hs.h().is_valid());
  EXPECT_FALSE(event.is_valid());
  EXPECT_EQ(hs.h().get(), handle);
}

TEST(Struct, AggregateInitializationMoveOnly) {
  zx::event event = MakeEvent();
  ASSERT_TRUE(event.is_valid());
  zx_handle_t handle = event.get();

  test_types::HandleStruct hs{{.h = std::move(event)}};
  EXPECT_TRUE(hs.h().is_valid());
  EXPECT_FALSE(event.is_valid());
  EXPECT_EQ(hs.h().get(), handle);
}

TEST(Struct, Equality) {
  EXPECT_EQ(test_types::EmptyStruct{}, test_types::EmptyStruct{});

  EXPECT_EQ(test_types::CopyableStruct{}, test_types::CopyableStruct{});
  EXPECT_EQ(test_types::CopyableStruct{{.x = 1}}, test_types::CopyableStruct{{.x = 1}});
  EXPECT_NE(test_types::CopyableStruct{{.x = 1}}, test_types::CopyableStruct{{.x = 2}});

  EXPECT_EQ(test_types::VectorStruct{}, test_types::VectorStruct{});
  std::vector<uint32_t> vec{1, 2, 3};
  EXPECT_EQ(test_types::VectorStruct{{.v = vec}}, test_types::VectorStruct{{.v = vec}});
  EXPECT_EQ(test_types::VectorStruct{{.v = vec}}, test_types::VectorStruct{{.v = vec}});
  EXPECT_NE(test_types::VectorStruct{{.v = vec}}, test_types::VectorStruct{{.v = {}}});
}

TEST(Struct, Setters) {
  test_types::CopyableStruct cs;
  EXPECT_EQ(cs.x(), 0);
  cs.x(1);
  EXPECT_EQ(cs.x(), 1);

  // Test chaining.
  test_types::StructWithPadding sp;
  sp.a(1).b(2);
  EXPECT_EQ(sp.a(), 1);
  EXPECT_EQ(sp.b(), 2u);

  auto sp2 = test_types::StructWithPadding{}.a(1).b(2);
  EXPECT_EQ(sp2.a(), 1);
  EXPECT_EQ(sp2.b(), 2u);
}

TEST(Struct, Accessors) {
  test_types::CopyableStruct cs{{.x = 1}};
  EXPECT_EQ(cs.x(), 1);
  EXPECT_EQ((cs.x() = 2), 2);
  EXPECT_EQ(cs.x(), 2);

  std::vector<uint32_t> vec{1, 2, 3};

  test_types::VectorStruct vs;
  EXPECT_EQ(vs.v().size(), 0UL);
  EXPECT_EQ((vs.v() = vec).size(), 3UL);
  EXPECT_EQ(vs.v().size(), 3UL);
  EXPECT_EQ(vec.size(), 3UL);
  vec.push_back(4);
  EXPECT_EQ(vec.size(), 4UL);
  EXPECT_EQ((vs.v() = std::move(vec)).size(), 4UL);
  EXPECT_EQ(vs.v().size(), 4UL);
  EXPECT_EQ(vec.size(), 0UL);
}

TEST(Struct, AccessorsAfterMove) {
  test_types::CopyableStruct cs{{.x = 1}};
  const test_types::CopyableStruct& const_cs = cs;
  test_types::VectorStruct vs{{.v = std::vector<uint32_t>{{1, 2, 3}}}};
  const test_types::VectorStruct const_vs;

  auto& mutable_x = cs.x();
  const auto& const_x = const_cs.x();
  auto& mutable_v = vs.v();
  const auto& const_v = const_vs.v();

  test_types::CopyableStruct moved_cs = std::move(cs);
  test_types::VectorStruct moved_vs = std::move(vs);

  moved_cs.x() = 2;
  ASSERT_EQ(mutable_x, 1);
  ASSERT_EQ(const_x, 1);
  ASSERT_EQ(mutable_v.size(), 0UL);
  ASSERT_EQ(const_v.size(), 0UL);
}

TEST(Struct, Copy) {
  test_types::VectorStruct original{{.v = std::vector<uint32_t>{{1, 2, 3}}}};
  test_types::VectorStruct copy = original;
  EXPECT_EQ(copy, original);
  original.v().push_back(4);
  EXPECT_NE(copy, original);
}

TEST(Struct, Move) {
  test_types::CopyableStruct cs{{.x = 1}};
  test_types::VectorStruct vs{{.v = std::vector<uint32_t>{{1, 2, 3}}}};
  test_types::HandleStruct hs{{.h = MakeEvent()}};
  ASSERT_TRUE(hs.h().is_valid());
  zx_handle_t handle = hs.h().get();

  test_types::CopyableStruct cs_moved = std::move(cs);
  test_types::VectorStruct vs_moved = std::move(vs);
  test_types::HandleStruct hs_moved = std::move(hs);

  EXPECT_EQ(cs.x(), 1);
  EXPECT_EQ(cs_moved.x(), 1);
  EXPECT_EQ(vs.v().size(), 0UL);
  EXPECT_EQ(vs_moved.v().size(), 3UL);
  EXPECT_FALSE(hs.h().is_valid());
  EXPECT_TRUE(hs_moved.h().is_valid());
  EXPECT_EQ(hs_moved.h().get(), handle);
}

template <typename T>
constexpr bool IsMemcpyCompatible =
    fidl::internal::NaturalIsMemcpyCompatible<T, fidl::internal::NaturalCodingConstraintEmpty>();

TEST(Struct, MemcpyCompatibility) {
  static_assert(IsMemcpyCompatible<test_types::StructWithoutPadding>);
  static_assert(IsMemcpyCompatible<test_types::FlexibleBits>);
  static_assert(IsMemcpyCompatible<test_types::FlexibleEnum>);

  static_assert(!IsMemcpyCompatible<test_types::EmptyStruct>);
  static_assert(!IsMemcpyCompatible<test_types::HandleStruct>);
  static_assert(!IsMemcpyCompatible<test_types::VectorStruct>);
  static_assert(!IsMemcpyCompatible<test_types::StructWithPadding>);
  static_assert(!IsMemcpyCompatible<test_types::StrictBits>);
  static_assert(!IsMemcpyCompatible<test_types::StrictEnum>);
  static_assert(!IsMemcpyCompatible<test_types::Uint64Table>);
  static_assert(!IsMemcpyCompatible<test_types::TestNonResourceXUnion>);
}

TEST(Struct, Traits) {
  static_assert(fidl::IsFidlType<test_types::StructWithoutPadding>::value);
  static_assert(fidl::IsStruct<test_types::StructWithoutPadding>::value);
  static_assert(!fidl::IsStruct<int>::value);
  static_assert(!fidl::IsStruct<test_types::FlexibleBits>::value);
  static_assert(fidl::TypeTraits<test_types::EmptyStruct>::kPrimarySize == 1);
  static_assert(fidl::TypeTraits<test_types::EmptyStruct>::kMaxOutOfLine == 0);
  static_assert(!fidl::TypeTraits<test_types::EmptyStruct>::kHasEnvelope);
}

}  // namespace
