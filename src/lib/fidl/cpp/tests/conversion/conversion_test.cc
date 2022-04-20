// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

#if __Fuchsia__
#include <fidl/test.types/cpp/fidl.h>
#include <lib/zx/event.h>
#else
#include <fidl/test.types/cpp/type_conversions.h>
#endif

TEST(WireToNaturalConversion, Primitives) {
  EXPECT_EQ(true, fidl::ToNatural(true));
  EXPECT_EQ(1u, fidl::ToNatural(1u));
  EXPECT_EQ(1ull, fidl::ToNatural(1ull));
  EXPECT_EQ(0.12, fidl::ToNatural(0.12));
}

TEST(WireNaturalConversionTraits, Enum) {
  EXPECT_EQ(test_types::StrictEnum::kB, fidl::ToNatural(test_types::wire::StrictEnum::kB));
  EXPECT_EQ(test_types::FlexibleEnum(100), fidl::ToNatural(test_types::wire::FlexibleEnum(100)));
}

TEST(WireToNaturalConversion, Bits) {
  EXPECT_EQ(test_types::StrictBits::kB | test_types::StrictBits::kD,
            fidl::ToNatural(test_types::wire::StrictBits::kB | test_types::wire::StrictBits::kD));
  EXPECT_EQ(test_types::FlexibleBits(100), fidl::ToNatural(test_types::wire::FlexibleBits(100)));
}

#if __Fuchsia__
TEST(WireToNaturalConversion, Handle) {
  zx::event ev;
  ASSERT_EQ(ZX_OK, zx::event::create(0, &ev));
  zx_handle_t handle = ev.get();

  {
    zx::event ev2 = fidl::ToNatural(std::move(ev));
    EXPECT_EQ(ZX_OK, ev2.get_info(ZX_INFO_HANDLE_VALID, nullptr, 0, nullptr, nullptr));
    EXPECT_EQ(handle, ev2.get());
  }
  EXPECT_EQ(ZX_ERR_BAD_HANDLE,
            zx_object_get_info(handle, ZX_INFO_HANDLE_VALID, nullptr, 0, nullptr, nullptr));
}

TEST(WireToNaturalConversion, InvalidHandle) {
  EXPECT_EQ(zx::handle(), fidl::ToNatural(zx::handle()));
}
#endif

TEST(WireToNaturalConversion, String) {
  EXPECT_EQ(std::string("abcd"), fidl::internal::ToNatural<std::string>(fidl::StringView("abcd")));
  EXPECT_EQ(std::string(), fidl::internal::ToNatural<std::string>(fidl::StringView()));
}

TEST(WireToNaturalConversion, OptionalString) {
  EXPECT_EQ(std::optional(std::string("abcd")), fidl::ToNatural(fidl::StringView("abcd")));
  EXPECT_EQ(std::optional(std::string("abcd")), fidl::ToNatural(fidl::StringView("abcd")));
  EXPECT_EQ(std::optional(std::string("abcd")), fidl::ToNatural(fidl::StringView("abcd")));
  EXPECT_EQ(std::nullopt, fidl::ToNatural(fidl::StringView()));
}

TEST(WireToNaturalConversion, Vector) {
  uint32_t data[] = {1, 2, 3};
  EXPECT_EQ(std::vector<uint32_t>(data, data + std::size(data)),
            fidl::internal::ToNatural<std::vector<uint32_t>>(
                fidl::VectorView<uint32_t>::FromExternal(data, std::size(data))));
}

TEST(WireToNaturalConversion, OptionalVector) {
  uint32_t data[] = {1, 2, 3};
  EXPECT_EQ(std::optional(std::vector<uint32_t>(data, data + std::size(data))),
            fidl::ToNatural(fidl::VectorView<uint32_t>::FromExternal(data, std::size(data))));
  EXPECT_EQ(std::nullopt, fidl::ToNatural(fidl::VectorView<uint32_t>()));
}

TEST(WireToNaturalConversion, ObjectView) {
  EXPECT_EQ(nullptr, fidl::ToNatural(fidl::ObjectView<test_types::CopyableStruct>()));

  fidl::Arena<512> arena;
  std::unique_ptr<test_types::CopyableStruct> val =
      fidl::ToNatural(fidl::ObjectView<test_types::wire::CopyableStruct>(
          arena, test_types::wire::CopyableStruct{.x = 123}));
  EXPECT_EQ(123, val->x());
}

TEST(WireToNaturalConversion, Union) {
  test_types::TestStrictXUnion xunion = fidl::internal::ToNatural<test_types::TestStrictXUnion>(
      test_types::wire::TestStrictXUnion::WithPrimitive(123));
  ASSERT_EQ(test_types::TestStrictXUnion::Tag::kPrimitive, xunion.Which());
  EXPECT_EQ(123, xunion.primitive().value());
}

TEST(WireToNaturalConversion, OptionalUnion) {
  ASSERT_EQ(nullptr, fidl::ToNatural(test_types::wire::TestStrictXUnion()));

  std::unique_ptr<test_types::TestStrictXUnion> xunion =
      fidl::ToNatural(test_types::wire::TestStrictXUnion::WithPrimitive(123));
  ASSERT_EQ(test_types::TestStrictXUnion::Tag::kPrimitive, xunion->Which());
  EXPECT_EQ(123, xunion->primitive().value());
}

TEST(WireToNaturalConversion, Table) {
  fidl::Arena<512> arena;
  test_types::SampleTable table =
      fidl::ToNatural(test_types::wire::SampleTable::Builder(arena).x(12).y(34).Build());
  EXPECT_EQ(12, table.x());
  EXPECT_EQ(34, table.y());
}

#if __Fuchsia__
TEST(WireToNaturalConversion, MoveOnlyUnion) {
  test_types::wire::TestXUnion wire = test_types::wire::TestXUnion::WithH(zx::handle());
  test_types::TestXUnion natural =
      fidl::internal::ToNatural<test_types::TestXUnion>(std::move(wire));
  EXPECT_FALSE(natural.h()->is_valid());
}

TEST(WireToNaturalConversion, MoveOnlyTable) {
  fidl::Arena<512> arena;
  test_types::TestHandleTable table =
      fidl::ToNatural(test_types::wire::TestHandleTable::Builder(arena).hs({}).Build());
  EXPECT_FALSE(table.hs()->h().is_valid());
}

TEST(WireToNaturalConversion, Request) {
  fidl::Request<test_types::Baz::Foo> request =
      fidl::ToNatural(fidl::WireRequest<test_types::Baz::Foo>({.bar = 123}));
  EXPECT_EQ(123, request.req().bar());
}

TEST(WireToNaturalConversion, Response) {
  fidl::Response<test_types::Baz::Foo> response =
      fidl::ToNatural(fidl::WireResponse<test_types::Baz::Foo>({.bar = 123}));
  EXPECT_EQ(123, response.res().bar());
}

TEST(WireToNaturalConversion, ResponseEmptyResultSuccess) {
  fidl::Response<test_types::ErrorSyntax::EmptyPayload> natural =
      fidl::ToNatural(fidl::WireResponse<test_types::ErrorSyntax::EmptyPayload>());
  ASSERT_TRUE(natural.is_ok());
}

TEST(WireToNaturalConversion, ResponseEmptyResultError) {
  fidl::WireResponse<test_types::ErrorSyntax::EmptyPayload> wire(
      test_types::wire::ErrorSyntaxEmptyPayloadResult::WithErr(123));
  fidl::Response<test_types::ErrorSyntax::EmptyPayload> natural = fidl::ToNatural(wire);
  ASSERT_TRUE(natural.is_error());
  EXPECT_EQ(123, natural.error_value());
}

TEST(WireToNaturalConversion, ResponseResultSuccess) {
  fidl::WireResponse<test_types::ErrorSyntax::FooPayload> wire(
      test_types::wire::ErrorSyntaxFooPayloadResult::WithResponse({.bar = 123}));
  fidl::Response<test_types::ErrorSyntax::FooPayload> natural = fidl::ToNatural(wire);
  ASSERT_TRUE(natural.is_ok());
  EXPECT_EQ(123, natural.value().bar());
}

TEST(WireToNaturalConversion, ResponseResultError) {
  fidl::WireResponse<test_types::ErrorSyntax::FooPayload> wire(
      test_types::wire::ErrorSyntaxFooPayloadResult::WithErr(123));
  fidl::Response<test_types::ErrorSyntax::FooPayload> natural = fidl::ToNatural(wire);
  ASSERT_TRUE(natural.is_error());
  EXPECT_EQ(123, natural.error_value());
}

TEST(WireToNaturalConversion, Event) {
  fidl::Event<test_types::Baz::FooEvent> event =
      fidl::ToNatural(fidl::WireEvent<test_types::Baz::FooEvent>(123));
  EXPECT_EQ(123, event.bar());
}
#endif
