// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/test.types/cpp/hlcpp_conversion.h>

#include <gtest/gtest.h>

#ifdef __Fuchsia__
#include <lib/zx/event.h>
#endif

TEST(StructConversion, CopyableToNatural) {
  test::types::CopyableStruct hlcpp;
  hlcpp.x = 42;
  auto natural = fidl::HLCPPToNatural(hlcpp);
  static_assert(std::is_same_v<decltype(natural), test_types::CopyableStruct>);
  EXPECT_EQ(natural.x(), 42);
}

TEST(StructConversion, CopyableToHLCPP) {
  test_types::CopyableStruct natural{{.x = 42}};
  auto hlcpp = fidl::NaturalToHLCPP(natural);
  static_assert(std::is_same_v<decltype(hlcpp), test::types::CopyableStruct>);
  EXPECT_EQ(hlcpp.x, 42);
}

#ifdef __Fuchsia__

TEST(StructConversion, MoveOnlyToNatural) {
  zx::event event;
  ASSERT_EQ(zx::event::create(0, &event), ZX_OK);
  zx_handle_t handle = event.get();
  ASSERT_NE(handle, ZX_HANDLE_INVALID);

  test::types::MoveOnlyStruct hlcpp;
  hlcpp.h = std::move(event);
  auto natural = fidl::HLCPPToNatural(hlcpp);
  static_assert(std::is_same_v<decltype(natural), test_types::MoveOnlyStruct>);
  EXPECT_EQ(natural.h().get(), handle);
  EXPECT_EQ(hlcpp.h.get(), ZX_HANDLE_INVALID);
}

TEST(StructConversion, MoveOnlyToHLCPP) {
  zx::event event;
  ASSERT_EQ(zx::event::create(0, &event), ZX_OK);
  zx_handle_t handle = event.get();
  ASSERT_NE(handle, ZX_HANDLE_INVALID);

  test_types::MoveOnlyStruct natural{{.h = std::move(event)}};
  auto hlcpp = fidl::NaturalToHLCPP(std::move(natural));
  static_assert(std::is_same_v<decltype(hlcpp), test::types::MoveOnlyStruct>);
  EXPECT_EQ(hlcpp.h.get(), handle);
  EXPECT_EQ(natural.h().get(), ZX_HANDLE_INVALID);
}

#endif

TEST(StructConversion, VectorToNatural) {
  test::types::VectorStruct hlcpp;
  hlcpp.v = std::vector<uint32_t>{{1, 2, 3, 4, 5, 7, 7}};
  auto natural = fidl::HLCPPToNatural(hlcpp);
  static_assert(std::is_same_v<decltype(natural), test_types::VectorStruct>);
  std::vector<uint32_t> v{{1, 2, 3, 4, 5, 7, 7}};
  EXPECT_EQ(natural.v(), v);
}

TEST(StructConversion, VectorToHLCPP) {
  std::vector<uint32_t> v{{1, 2, 3, 4, 5, 7, 7}};
  test_types::VectorStruct natural;
  natural.v() = v;
  auto hlcpp = fidl::NaturalToHLCPP(natural);
  static_assert(std::is_same_v<decltype(hlcpp), test::types::VectorStruct>);
  EXPECT_EQ(hlcpp.v, v);
}

TEST(StructConversion, EmptyToNatural) {
  test::types::EmptyStruct hlcpp;
  auto natural = fidl::HLCPPToNatural(hlcpp);
  static_assert(std::is_same_v<decltype(natural), test_types::EmptyStruct>);
}

TEST(StructConversion, EmptyToHLCPP) {
  test_types::EmptyStruct natural;
  auto hlcpp = fidl::NaturalToHLCPP(natural);
  static_assert(std::is_same_v<decltype(hlcpp), test::types::EmptyStruct>);
}

TEST(StructConversion, StrictUnionInArrayToNatural) {
  ::test::types::CopyableStruct cs;
  cs.x = 23;
  std::array<test::types::TestStrictXUnion, 10> arr{
      test::types::TestStrictXUnion::WithCopyable(std::move(cs)),
      test::types::TestStrictXUnion::WithPrimitive(1),
      test::types::TestStrictXUnion::WithPrimitive(2),
      test::types::TestStrictXUnion::WithPrimitive(3),
      test::types::TestStrictXUnion::WithPrimitive(4),
      test::types::TestStrictXUnion::WithPrimitive(5),
      test::types::TestStrictXUnion::WithPrimitive(6),
      test::types::TestStrictXUnion::WithPrimitive(7),
      test::types::TestStrictXUnion::WithPrimitive(8),
      test::types::TestStrictXUnion::WithPrimitive(9),
  };
  test::types::TestStrictXUnionInArrayInStruct hlcpp;
  hlcpp.arr = std::move(arr);
  auto natural = fidl::HLCPPToNatural(hlcpp);
  static_assert(std::is_same_v<decltype(natural), test_types::TestStrictXUnionInArrayInStruct>);
  ASSERT_EQ(natural.arr()[0].Which(), test_types::TestStrictXUnion::Tag::kCopyable);
  EXPECT_EQ(natural.arr()[0].copyable().value().x(), 23);
  for (size_t i = 1; i < natural.arr().size(); i++) {
    ASSERT_EQ(natural.arr()[i].Which(), test_types::TestStrictXUnion::Tag::kPrimitive);
    EXPECT_EQ(natural.arr()[i].primitive().value(), (int)i);
  }
}

TEST(StructConversion, StrictUnionInArrayToHLCPP) {
  ::test_types::CopyableStruct cs{{.x = 23}};
  std::array<test_types::TestStrictXUnion, 10> arr{
      test_types::TestStrictXUnion::WithCopyable(std::move(cs)),
      test_types::TestStrictXUnion::WithPrimitive(1),
      test_types::TestStrictXUnion::WithPrimitive(2),
      test_types::TestStrictXUnion::WithPrimitive(3),
      test_types::TestStrictXUnion::WithPrimitive(4),
      test_types::TestStrictXUnion::WithPrimitive(5),
      test_types::TestStrictXUnion::WithPrimitive(6),
      test_types::TestStrictXUnion::WithPrimitive(7),
      test_types::TestStrictXUnion::WithPrimitive(8),
      test_types::TestStrictXUnion::WithPrimitive(9),
  };
  test_types::TestStrictXUnionInArrayInStruct natural{{.arr = arr}};
  auto hlcpp = fidl::NaturalToHLCPP(natural);
  static_assert(std::is_same_v<decltype(hlcpp), test::types::TestStrictXUnionInArrayInStruct>);
  ASSERT_EQ(hlcpp.arr[0].Which(), test::types::TestStrictXUnion::Tag::kCopyable);
  EXPECT_EQ(hlcpp.arr[0].copyable().x, 23);
  for (size_t i = 1; i < hlcpp.arr.size(); i++) {
    ASSERT_EQ(hlcpp.arr[i].Which(), test::types::TestStrictXUnion::Tag::kPrimitive);
    EXPECT_EQ(hlcpp.arr[i].primitive(), (int)i);
  }
}

TEST(StructConversion, OptionalMembersToNatural) {
  {
    test::types::StructOfOptionals hlcpp;
    auto natural = fidl::HLCPPToNatural(std::move(hlcpp));
    static_assert(std::is_same_v<decltype(natural), test_types::StructOfOptionals>);
    EXPECT_FALSE(natural.s().has_value());
    EXPECT_FALSE(natural.v().has_value());
    EXPECT_FALSE(natural.t());
  }

  {
    test::types::StructOfOptionals hlcpp;
    hlcpp.s = "Hello, world";
    auto natural = fidl::HLCPPToNatural(std::move(hlcpp));
    static_assert(std::is_same_v<decltype(natural), test_types::StructOfOptionals>);
    EXPECT_EQ(natural.s(), std::make_optional<std::string>("Hello, world"));
    EXPECT_FALSE(natural.v().has_value());
    EXPECT_FALSE(natural.t());
  }
  {
    test::types::StructOfOptionals hlcpp;
    hlcpp.s = "Hello, world";
    hlcpp.v = {2, 3, 4, 5};
    hlcpp.t = std::make_unique<test::types::CopyableStruct>();
    hlcpp.t->x = 42;
    auto natural = fidl::HLCPPToNatural(std::move(hlcpp));
    static_assert(std::is_same_v<decltype(natural), test_types::StructOfOptionals>);
    EXPECT_EQ(natural.s(), std::make_optional<std::string>("Hello, world"));
    std::vector<std::uint32_t> v{2, 3, 4, 5};
    EXPECT_EQ(natural.v(), std::make_optional<std::vector<std::uint32_t>>(v));
    ASSERT_TRUE(natural.t());
    EXPECT_EQ(natural.t()->x(), 42);
  }
}

TEST(StructConversion, OptionalMembersToHLCPP) {
  {
    test_types::StructOfOptionals natural;
    auto hlcpp = fidl::NaturalToHLCPP(std::move(natural));
    static_assert(std::is_same_v<decltype(hlcpp), test::types::StructOfOptionals>);
    EXPECT_FALSE(hlcpp.s.has_value());
    EXPECT_FALSE(hlcpp.v.has_value());
    EXPECT_FALSE(hlcpp.t);
  }

  {
    test_types::StructOfOptionals natural({.s = "Hello, world"});
    auto hlcpp = fidl::NaturalToHLCPP(std::move(natural));
    static_assert(std::is_same_v<decltype(hlcpp), test::types::StructOfOptionals>);
    EXPECT_EQ(hlcpp.s, std::make_optional<std::string>("Hello, world"));
    EXPECT_FALSE(hlcpp.v.has_value());
    EXPECT_FALSE(hlcpp.t);
  }
  {
    test_types::StructOfOptionals natural;
    natural.s() = "Hello, world";
    natural.v() = {2, 3, 4, 5};
    natural.t() = std::make_unique<test_types::CopyableStruct>();
    natural.t()->x() = 42;
    auto hlcpp = fidl::NaturalToHLCPP(std::move(natural));
    static_assert(std::is_same_v<decltype(hlcpp), test::types::StructOfOptionals>);
    EXPECT_EQ(hlcpp.s, std::make_optional<std::string>("Hello, world"));
    std::vector<std::uint32_t> v{2, 3, 4, 5};
    EXPECT_EQ(hlcpp.v, std::make_optional<std::vector<std::uint32_t>>(v));
    ASSERT_TRUE(hlcpp.t);
    EXPECT_EQ(hlcpp.t->x, 42);
  }
}
