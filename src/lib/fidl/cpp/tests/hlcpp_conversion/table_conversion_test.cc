// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/test.types/cpp/hlcpp_conversion.h>

#include <gtest/gtest.h>

#ifdef __Fuchsia__
#include <lib/zx/event.h>
#endif

TEST(TableConversion, SimpleToNatural) {
  test::types::TestTable hlcpp;
  hlcpp.set_x(42);
  auto natural = fidl::HLCPPToNatural(hlcpp);
  static_assert(std::is_same_v<decltype(natural), test_types::TestTable>);
  EXPECT_EQ(natural.x(), 42);
}

TEST(TableConversion, SimpleToHLCPP) {
  test_types::TestTable natural{{.x = 42}};
  auto hlcpp = fidl::NaturalToHLCPP(natural);
  static_assert(std::is_same_v<decltype(hlcpp), test::types::TestTable>);
  EXPECT_EQ(hlcpp.x(), 42);
}

TEST(TableConversion, UnsetToNatural) {
  test::types::TestTable hlcpp;
  auto natural = fidl::HLCPPToNatural(hlcpp);
  static_assert(std::is_same_v<decltype(natural), test_types::TestTable>);
  EXPECT_FALSE(natural.x().has_value());
}

TEST(TableConversion, UnsetToHLCPP) {
  test_types::TestTable natural;
  auto hlcpp = fidl::NaturalToHLCPP(natural);
  static_assert(std::is_same_v<decltype(hlcpp), test::types::TestTable>);
  EXPECT_FALSE(hlcpp.has_x());
}

TEST(TableConversion, EmptyToNatural) {
  test::types::SampleEmptyTable hlcpp;
  auto natural = fidl::HLCPPToNatural(hlcpp);
  static_assert(std::is_same_v<decltype(natural), test_types::SampleEmptyTable>);
}

TEST(TableConversion, EmptyToHLCPP) {
  test_types::SampleEmptyTable natural;
  auto hlcpp = fidl::NaturalToHLCPP(natural);
  static_assert(std::is_same_v<decltype(hlcpp), test::types::SampleEmptyTable>);
}

TEST(TableConversion, ResourceToNatural) {
  test::types::TestResourceTable hlcpp;
  hlcpp.set_x(42);
  auto natural = fidl::HLCPPToNatural(hlcpp);
  static_assert(std::is_same_v<decltype(natural), test_types::TestResourceTable>);
  EXPECT_EQ(natural.x(), 42);
}

TEST(TableConversion, ResourceToHLCPP) {
  test_types::TestResourceTable natural{{.x = 42}};
  auto hlcpp = fidl::NaturalToHLCPP(natural);
  static_assert(std::is_same_v<decltype(hlcpp), test::types::TestResourceTable>);
  EXPECT_EQ(hlcpp.x(), 42);
}

TEST(TableConversion, MultipleToNatural) {
  test::types::SampleTable hlcpp;
  hlcpp.set_x(42);
  hlcpp.set_y(23);
  hlcpp.set_b(true);
  auto natural = fidl::HLCPPToNatural(hlcpp);
  static_assert(std::is_same_v<decltype(natural), test_types::SampleTable>);
  EXPECT_EQ(natural.x(), 42);
  EXPECT_EQ(natural.y(), 23);
  EXPECT_EQ(natural.b().value(), true);
  EXPECT_FALSE(natural.vector_of_struct().has_value());
  EXPECT_FALSE(natural.s().has_value());
}

TEST(TableConversion, MultipleToHLCPP) {
  test_types::SampleTable natural{{.x = 42, .y = 23, .b = true}};
  auto hlcpp = fidl::NaturalToHLCPP(natural);
  static_assert(std::is_same_v<decltype(hlcpp), test::types::SampleTable>);
  EXPECT_EQ(hlcpp.x(), 42);
  EXPECT_EQ(hlcpp.y(), 23);
  EXPECT_EQ(hlcpp.b(), true);
  EXPECT_FALSE(hlcpp.has_vector_of_struct());
  EXPECT_FALSE(hlcpp.has_s());
}

#ifdef __Fuchsia__
TEST(TableConversion, HandleToNatural) {
  zx::event event;
  ASSERT_EQ(zx::event::create(0, &event), ZX_OK);
  zx_handle_t handle = event.get();
  ASSERT_NE(handle, ZX_HANDLE_INVALID);

  test::types::HandleStruct hlcpp_hs;
  hlcpp_hs.h = std::move(event);
  test::types::TestHandleTable hlcpp;
  hlcpp.set_hs(std::move(hlcpp_hs));
  auto natural = fidl::HLCPPToNatural(hlcpp);
  static_assert(std::is_same_v<decltype(natural), test_types::TestHandleTable>);
  ASSERT_TRUE(natural.hs().has_value());
  EXPECT_EQ(natural.hs().value().h().get(), handle);
  EXPECT_EQ(hlcpp.hs().h.get(), ZX_HANDLE_INVALID);
}

TEST(TableConversion, HandleToHLCPP) {
  zx::event event;
  ASSERT_EQ(zx::event::create(0, &event), ZX_OK);
  zx_handle_t handle = event.get();
  ASSERT_NE(handle, ZX_HANDLE_INVALID);

  test_types::TestHandleTable natural{{.hs = test_types::HandleStruct({.h = std::move(event)})}};
  auto hlcpp = fidl::NaturalToHLCPP(natural);
  static_assert(std::is_same_v<decltype(hlcpp), test::types::TestHandleTable>);
  ASSERT_TRUE(hlcpp.has_hs());
  EXPECT_EQ(hlcpp.hs().h.get(), handle);
  EXPECT_EQ(natural.hs()->h().get(), ZX_HANDLE_INVALID);
}

#endif
