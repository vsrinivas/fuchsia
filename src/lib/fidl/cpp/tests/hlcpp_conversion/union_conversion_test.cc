// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/test.types/cpp/hlcpp_conversion.h>

#include <gtest/gtest.h>

#ifdef __Fuchsia__
#include <lib/zx/event.h>
#endif

TEST(UnionConversion, StrictToNatural) {
  auto primitive = fidl::HLCPPToNatural(test::types::TestUnion::WithPrimitive(42));
  ASSERT_TRUE(primitive.primitive().has_value())
      << "Union tag: " << static_cast<fidl_xunion_tag_t>(primitive.Which());
  EXPECT_EQ(primitive.primitive().value(), 42);

  auto copyable = fidl::HLCPPToNatural(
      test::types::TestUnion::WithCopyable(test::types::CopyableStruct{.x = 23}));
  ASSERT_TRUE(copyable.copyable().has_value())
      << "Union tag: " << static_cast<fidl_xunion_tag_t>(copyable.Which());
  EXPECT_EQ(copyable.copyable().value().x(), 23);
}

TEST(UnionConversion, StrictToHLCPP) {
  auto primitive = fidl::NaturalToHLCPP(test_types::TestUnion::WithPrimitive(42));
  ASSERT_TRUE(primitive.is_primitive())
      << "Union tag: " << static_cast<fidl_xunion_tag_t>(primitive.Which());
  EXPECT_EQ(primitive.primitive(), 42);

  auto copyable = fidl::NaturalToHLCPP(
      test_types::TestUnion::WithCopyable(test_types::CopyableStruct{{.x = 23}}));
  ASSERT_TRUE(copyable.is_copyable())
      << "Union tag: " << static_cast<fidl_xunion_tag_t>(copyable.Which());
  EXPECT_EQ(copyable.copyable().x, 23);
}

#ifdef __Fuchsia__

TEST(UnionConversion, FlexibleToNatural) {
  auto primitive = fidl::HLCPPToNatural(test::types::TestXUnion::WithPrimitive(42));
  ASSERT_TRUE(primitive.primitive().has_value())
      << "Union tag: " << static_cast<fidl_xunion_tag_t>(primitive.Which());
  EXPECT_EQ(primitive.primitive().value(), 42);

  auto copyable = fidl::HLCPPToNatural(
      test::types::TestXUnion::WithCopyable(test::types::CopyableStruct{.x = 23}));
  ASSERT_TRUE(copyable.copyable().has_value())
      << "Union tag: " << static_cast<fidl_xunion_tag_t>(copyable.Which());
  EXPECT_EQ(copyable.copyable().value().x(), 23);

  auto unknown = fidl::HLCPPToNatural(test::types::TestXUnion{});
  ASSERT_TRUE(unknown.IsUnknown())
      << "Union tag: " << static_cast<fidl_xunion_tag_t>(unknown.Which());
}

TEST(UnionConversion, FlexibleToHLCPP) {
  auto primitive = fidl::NaturalToHLCPP(test_types::TestXUnion::WithPrimitive(42));
  ASSERT_TRUE(primitive.is_primitive())
      << "Union tag: " << static_cast<fidl_xunion_tag_t>(primitive.Which());
  EXPECT_EQ(primitive.primitive(), 42);

  auto copyable = fidl::NaturalToHLCPP(
      test_types::TestXUnion::WithCopyable(test_types::CopyableStruct{{.x = 23}}));
  ASSERT_TRUE(copyable.is_copyable())
      << "Union tag: " << static_cast<fidl_xunion_tag_t>(copyable.Which());
  EXPECT_EQ(copyable.copyable().x, 23);

  // Use an internal API to make an unknown union.
  // In practice, this will only happen during IPC and when the sender/receiver
  // schema mismatch.
  auto unknown = fidl::NaturalToHLCPP(
      test_types::TestXUnion(fidl::internal::DefaultConstructPossiblyInvalidObjectTag{}));
  ASSERT_TRUE(unknown.has_invalid_tag())
      << "Union tag: " << static_cast<fidl_xunion_tag_t>(unknown.Which());
}

TEST(UnionConversion, HandleToNatural) {
  zx::event event;
  ASSERT_EQ(zx::event::create(0, &event), ZX_OK);
  zx_handle_t handle = event.get();
  ASSERT_NE(handle, ZX_HANDLE_INVALID);

  auto hlcpp = test::types::TestXUnion::WithH(std::move(event));
  auto natural = fidl::HLCPPToNatural(std::move(hlcpp));
  static_assert(std::is_same_v<decltype(natural), test_types::TestXUnion>);
  ASSERT_TRUE(natural.h().has_value())
      << "Union tag: " << static_cast<fidl_xunion_tag_t>(natural.Which());
  EXPECT_EQ(natural.h()->get(), handle);
  EXPECT_EQ(hlcpp.h().get(), ZX_HANDLE_INVALID);
}

TEST(UnionConversion, HandleToHLCPP) {
  zx::event event;
  ASSERT_EQ(zx::event::create(0, &event), ZX_OK);
  zx_handle_t handle = event.get();
  ASSERT_NE(handle, ZX_HANDLE_INVALID);

  auto natural = test_types::TestXUnion::WithH(std::move(event));
  auto hlcpp = fidl::NaturalToHLCPP(std::move(natural));
  ASSERT_TRUE(hlcpp.is_h()) << "Union tag: " << static_cast<fidl_xunion_tag_t>(hlcpp.Which());
  EXPECT_EQ(hlcpp.h().get(), handle);
  EXPECT_EQ(natural.h()->get(), ZX_HANDLE_INVALID);
}

#endif
