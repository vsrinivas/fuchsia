// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>
#include <src/lib/fidl/llcpp/tests/arena_checker.h>

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

TEST(NaturalToWireConversion, Primitives) {
  fidl::Arena arena;
  EXPECT_EQ(true, fidl::ToWire(arena, true));
  EXPECT_EQ(1u, fidl::ToWire(arena, 1u));
  EXPECT_EQ(1ull, fidl::ToWire(arena, 1ull));
  EXPECT_EQ(0.12, fidl::ToWire(arena, 0.12));
}

TEST(WireNaturalConversionTraits, Enum) {
  EXPECT_EQ(test_types::StrictEnum::kB, fidl::ToNatural(test_types::wire::StrictEnum::kB));
  EXPECT_EQ(test_types::FlexibleEnum(100), fidl::ToNatural(test_types::wire::FlexibleEnum(100)));
}

TEST(NaturalToWireConversion, Enum) {
  fidl::Arena arena;
  EXPECT_EQ(test_types::wire::StrictEnum::kB, fidl::ToWire(arena, test_types::StrictEnum::kB));
  EXPECT_EQ(test_types::wire::FlexibleEnum(100),
            fidl::ToWire(arena, test_types::FlexibleEnum(100)));
}

TEST(WireToNaturalConversion, Bits) {
  EXPECT_EQ(test_types::StrictBits::kB | test_types::StrictBits::kD,
            fidl::ToNatural(test_types::wire::StrictBits::kB | test_types::wire::StrictBits::kD));
  EXPECT_EQ(test_types::FlexibleBits(100), fidl::ToNatural(test_types::wire::FlexibleBits(100)));
}

TEST(NaturalToWireConversion, Bits) {
  fidl::Arena arena;
  EXPECT_EQ(test_types::wire::StrictBits::kB | test_types::wire::StrictBits::kD,
            fidl::ToWire(arena, test_types::StrictBits::kB | test_types::StrictBits::kD));
  EXPECT_EQ(test_types::wire::FlexibleBits(100),
            fidl::ToWire(arena, test_types::FlexibleBits(100)));
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

TEST(NaturalToWireConversion, Handle) {
  fidl::Arena arena;
  zx::event ev;
  ASSERT_EQ(ZX_OK, zx::event::create(0, &ev));
  zx_handle_t handle = ev.get();

  {
    zx::event ev2 = fidl::ToWire(arena, std::move(ev));
    EXPECT_EQ(ZX_OK, ev2.get_info(ZX_INFO_HANDLE_VALID, nullptr, 0, nullptr, nullptr));
    EXPECT_EQ(handle, ev2.get());
  }
  EXPECT_EQ(ZX_ERR_BAD_HANDLE,
            zx_object_get_info(handle, ZX_INFO_HANDLE_VALID, nullptr, 0, nullptr, nullptr));
}

TEST(WireToNaturalConversion, InvalidHandle) {
  EXPECT_EQ(zx::handle(), fidl::ToNatural(zx::handle()));
}

TEST(NaturalToWireConversion, InvalidHandle) {
  fidl::Arena arena;
  EXPECT_EQ(zx::handle(), fidl::ToWire(arena, zx::handle()));
}

TEST(WireToNaturalConversion, ClientEnd) {
  static_assert(std::is_same_v<decltype(fidl::ToNatural(fidl::ClientEnd<test_types::Baz>())),
                               fidl::ClientEnd<test_types::Baz>>);
  EXPECT_EQ(fidl::ClientEnd<test_types::Baz>(),
            fidl::ToNatural(fidl::ClientEnd<test_types::Baz>()));
}

TEST(NaturalToWireConversion, ClientEnd) {
  static_assert(std::is_same_v<decltype(fidl::ToNatural(fidl::ClientEnd<test_types::Baz>())),
                               fidl::ClientEnd<test_types::Baz>>);
  fidl::Arena arena;
  EXPECT_EQ(fidl::ClientEnd<test_types::Baz>(),
            fidl::ToWire(arena, fidl::ClientEnd<test_types::Baz>()));
}

TEST(WireToNaturalConversion, ServerEnd) {
  static_assert(std::is_same_v<decltype(fidl::ToNatural(fidl::ServerEnd<test_types::Baz>())),
                               fidl::ServerEnd<test_types::Baz>>);
  EXPECT_EQ(fidl::ServerEnd<test_types::Baz>(),
            fidl::ToNatural(fidl::ServerEnd<test_types::Baz>()));
}

TEST(NaturalToWireConversion, ServerEnd) {
  static_assert(std::is_same_v<decltype(fidl::ToNatural(fidl::ServerEnd<test_types::Baz>())),
                               fidl::ServerEnd<test_types::Baz>>);
  fidl::Arena arena;
  EXPECT_EQ(fidl::ServerEnd<test_types::Baz>(),
            fidl::ToWire(arena, fidl::ServerEnd<test_types::Baz>()));
}
#endif

TEST(WireToNaturalConversion, String) {
  EXPECT_EQ(std::string("abcd"), fidl::internal::ToNatural<std::string>(fidl::StringView("abcd")));
  EXPECT_EQ(std::string(), fidl::internal::ToNatural<std::string>(fidl::StringView("")));
  EXPECT_EQ(std::string(), fidl::internal::ToNatural<std::string>(fidl::StringView()));
}

TEST(NaturalToWireConversion, String) {
  fidl::Arena arena;
  fidl::StringView str = fidl::ToWire(arena, std::string("abcd"));
  EXPECT_EQ("abcd", str.get());
  EXPECT_TRUE(fidl_testing::ArenaChecker::IsPointerInArena(str.data(), arena));

  fidl::StringView empty_str = fidl::ToWire(arena, std::string(""));
  EXPECT_EQ(0u, empty_str.size());
  EXPECT_NE(nullptr, empty_str.data());

  fidl::StringView null_str = fidl::ToWire(arena, std::string());
  EXPECT_EQ(0u, null_str.size());
  EXPECT_NE(nullptr, null_str.data());
}

TEST(WireToNaturalConversion, OptionalString) {
  EXPECT_EQ(std::optional(std::string("abcd")), fidl::ToNatural(fidl::StringView("abcd")));
  EXPECT_EQ(std::nullopt, fidl::ToNatural(fidl::StringView()));
}

TEST(NaturalToWireConversion, OptionalString) {
  fidl::Arena arena;
  fidl::StringView str = fidl::ToWire(arena, std::optional(std::string("abcd")));
  EXPECT_EQ("abcd", str.get());
  EXPECT_TRUE(fidl_testing::ArenaChecker::IsPointerInArena(str.data(), arena));

  fidl::StringView empty_str = fidl::ToWire(arena, std::optional(std::string("")));
  EXPECT_EQ(0u, empty_str.size());
  EXPECT_NE(nullptr, empty_str.data());

  fidl::StringView null_str = fidl::ToWire(arena, std::optional(std::string()));
  EXPECT_EQ(0u, null_str.size());
  EXPECT_NE(nullptr, null_str.data());

  fidl::StringView nullopt_str = fidl::ToWire(arena, std::optional<std::string>());
  EXPECT_EQ(0u, nullopt_str.size());
  EXPECT_EQ(nullptr, nullopt_str.data());
}

TEST(WireToNaturalConversion, Vector) {
  uint32_t data[] = {1, 2, 3};
  EXPECT_EQ(std::vector<uint32_t>(data, data + std::size(data)),
            fidl::internal::ToNatural<std::vector<uint32_t>>(
                fidl::VectorView<uint32_t>::FromExternal(data, std::size(data))));
}

TEST(NaturalToWireConversion, Vector) {
  fidl::Arena arena;
  fidl::VectorView<uint32_t> vec = fidl::ToWire(arena, std::vector<uint32_t>{1, 2, 3});
  EXPECT_EQ(3u, vec.count());
  EXPECT_EQ(1u, vec[0]);
  EXPECT_EQ(2u, vec[1]);
  EXPECT_EQ(3u, vec[2]);
  EXPECT_TRUE(fidl_testing::ArenaChecker::IsPointerInArena(vec.data(), arena));

  fidl::VectorView<uint32_t> empty_vec = fidl::ToWire(arena, std::vector<uint32_t>({}));
  EXPECT_EQ(0u, empty_vec.count());
  EXPECT_NE(nullptr, empty_vec.data());

  fidl::VectorView<uint32_t> null_vec = fidl::ToWire(arena, std::vector<uint32_t>());
  EXPECT_EQ(0u, null_vec.count());
  EXPECT_NE(nullptr, null_vec.data());
}

TEST(WireToNaturalConversion, OptionalVector) {
  uint32_t data[] = {1, 2, 3};
  EXPECT_EQ(std::optional(std::vector<uint32_t>(data, data + std::size(data))),
            fidl::ToNatural(fidl::VectorView<uint32_t>::FromExternal(data, std::size(data))));
  EXPECT_EQ(std::nullopt, fidl::ToNatural(fidl::VectorView<uint32_t>()));
}

TEST(NaturalToWireConversion, OptionalVector) {
  fidl::Arena arena;
  fidl::VectorView<uint32_t> vec =
      fidl::ToWire(arena, std::optional(std::vector<uint32_t>{1, 2, 3}));
  EXPECT_EQ(3u, vec.count());
  EXPECT_EQ(1u, vec[0]);
  EXPECT_EQ(2u, vec[1]);
  EXPECT_EQ(3u, vec[2]);
  EXPECT_TRUE(fidl_testing::ArenaChecker::IsPointerInArena(vec.data(), arena));

  fidl::VectorView<uint32_t> empty_vec =
      fidl::ToWire(arena, std::optional(std::vector<uint32_t>({})));
  EXPECT_EQ(0u, empty_vec.count());
  EXPECT_NE(nullptr, empty_vec.data());

  fidl::VectorView<uint32_t> null_vec = fidl::ToWire(arena, std::optional(std::vector<uint32_t>()));
  EXPECT_EQ(0u, null_vec.count());
  EXPECT_NE(nullptr, null_vec.data());

  fidl::VectorView<uint32_t> nullopt_vec =
      fidl::ToWire(arena, std::optional<std::vector<uint32_t>>());
  EXPECT_EQ(0u, nullopt_vec.count());
  EXPECT_EQ(nullptr, nullopt_vec.data());
}

TEST(WireToNaturalConversion, ObjectView) {
  EXPECT_EQ(nullptr, fidl::ToNatural(fidl::ObjectView<test_types::wire::CopyableStruct>()));

  fidl::Arena<512> arena;
  std::unique_ptr<test_types::CopyableStruct> val =
      fidl::ToNatural(fidl::ObjectView<test_types::wire::CopyableStruct>(
          arena, test_types::wire::CopyableStruct{.x = 123}));
  EXPECT_EQ(123, val->x());
}

TEST(NaturalToWireConversion, ObjectView) {
  fidl::Arena arena;
  EXPECT_EQ(nullptr, fidl::ToWire(arena, std::unique_ptr<test_types::CopyableStruct>(nullptr)));

  fidl::ObjectView<test_types::wire::CopyableStruct> val =
      fidl::ToWire(arena, std::make_unique<test_types::CopyableStruct>(123));
  EXPECT_EQ(123, val->x);
  EXPECT_TRUE(fidl_testing::ArenaChecker::IsPointerInArena(val.get(), arena));
}

TEST(WireToNaturalConversion, Union) {
  fidl::Arena arena;

  test_types::TestStrictXUnion union_with_uint32 =
      fidl::internal::ToNatural<test_types::TestStrictXUnion>(
          test_types::wire::TestStrictXUnion::WithPrimitive(123));
  ASSERT_EQ(test_types::TestStrictXUnion::Tag::kPrimitive, union_with_uint32.Which());
  EXPECT_EQ(123, union_with_uint32.primitive().value());

  test_types::UnionWithUint64 union_with_uint64 =
      fidl::internal::ToNatural<test_types::UnionWithUint64>(
          test_types::wire::UnionWithUint64::WithValue(arena, 123));
  ASSERT_EQ(test_types::UnionWithUint64::Tag::kValue, union_with_uint64.Which());
  EXPECT_EQ(123ll, union_with_uint64.value().value());
}

TEST(NaturalToWireConversion, Union) {
  fidl::Arena arena;

  test_types::wire::TestStrictXUnion union_with_uint32 =
      fidl::ToWire(arena, test_types::TestStrictXUnion::WithPrimitive(123));
  ASSERT_EQ(test_types::wire::TestStrictXUnion::Tag::kPrimitive, union_with_uint32.Which());
  EXPECT_EQ(123, union_with_uint32.primitive());
  // Inline union value.
  EXPECT_FALSE(fidl_testing::ArenaChecker::IsPointerInArena(&union_with_uint32.primitive(), arena));

  test_types::wire::UnionWithUint64 union_with_uint64 =
      fidl::ToWire(arena, test_types::UnionWithUint64::WithValue(123));
  ASSERT_EQ(test_types::wire::UnionWithUint64::Tag::kValue, union_with_uint64.Which());
  EXPECT_EQ(123ll, union_with_uint64.value());
  // Inline union value.
  EXPECT_TRUE(fidl_testing::ArenaChecker::IsPointerInArena(&union_with_uint64.value(), arena));
}

TEST(WireToNaturalConversion, OptionalUnion) {
  fidl::Arena arena;

  ASSERT_EQ(nullptr, fidl::ToNatural(test_types::wire::TestStrictXUnion()));

  std::unique_ptr<test_types::TestStrictXUnion> union_with_uint32 =
      fidl::ToNatural(test_types::wire::TestStrictXUnion::WithPrimitive(123));
  ASSERT_EQ(test_types::TestStrictXUnion::Tag::kPrimitive, union_with_uint32->Which());
  EXPECT_EQ(123, union_with_uint32->primitive().value());

  std::unique_ptr<test_types::UnionWithUint64> union_with_uint64 =
      fidl::ToNatural(test_types::wire::UnionWithUint64::WithValue(arena, 123ll));
  ASSERT_EQ(test_types::UnionWithUint64::Tag::kValue, union_with_uint64->Which());
  EXPECT_EQ(123ll, union_with_uint64->value().value());
}

TEST(NaturalToWireConversion, OptionalUnion) {
  fidl::Arena arena;

  test_types::wire::TestStrictXUnion empty =
      fidl::ToWire(arena, std::unique_ptr<test_types::TestStrictXUnion>());
  ASSERT_TRUE(empty.has_invalid_tag());

  test_types::wire::TestStrictXUnion xunion =
      fidl::ToWire(arena, std::make_unique<test_types::TestStrictXUnion>(
                              test_types::TestStrictXUnion::WithPrimitive(123)));
  ASSERT_EQ(test_types::wire::TestStrictXUnion::Tag::kPrimitive, xunion.Which());
  EXPECT_EQ(123, xunion.primitive());
  // Inline union value.
  EXPECT_FALSE(fidl_testing::ArenaChecker::IsPointerInArena(&xunion.primitive(), arena));

  test_types::wire::UnionWithUint64 union_with_uint64 = fidl::ToWire(
      arena,
      std::make_unique<test_types::UnionWithUint64>(test_types::UnionWithUint64::WithValue(123ll)));
  ASSERT_EQ(test_types::wire::UnionWithUint64::Tag::kValue, union_with_uint64.Which());
  EXPECT_EQ(123ll, union_with_uint64.value());
  // Inline union value.
  EXPECT_TRUE(fidl_testing::ArenaChecker::IsPointerInArena(&union_with_uint64.value(), arena));
}

TEST(WireToNaturalConversion, Table) {
  fidl::Arena<512> arena;
  test_types::SampleTable table =
      fidl::ToNatural(test_types::wire::SampleTable::Builder(arena).x(12).y(34).Build());
  EXPECT_EQ(12, table.x());
  EXPECT_EQ(34, table.y());
}

TEST(NaturalToWireConversion, Table) {
  fidl::Arena arena;
  test_types::wire::SampleTable table = fidl::ToWire(arena, test_types::SampleTable({
                                                                .x = 12,
                                                                .y = 34,
                                                            }));
  EXPECT_EQ(12, table.x());
  EXPECT_EQ(34, table.y());
  EXPECT_TRUE(fidl_testing::ArenaChecker::IsPointerInArena(&table.x(), arena));
  EXPECT_TRUE(fidl_testing::ArenaChecker::IsPointerInArena(&table.y(), arena));

  void* frame = reinterpret_cast<fidl_vector_t*>(&table)->data;
  EXPECT_TRUE(fidl_testing::ArenaChecker::IsPointerInArena(frame, arena));
}

#if __Fuchsia__
TEST(WireToNaturalConversion, MoveOnlyUnion) {
  test_types::wire::TestXUnion wire = test_types::wire::TestXUnion::WithH(zx::handle());
  test_types::TestXUnion natural =
      fidl::internal::ToNatural<test_types::TestXUnion>(std::move(wire));
  EXPECT_FALSE(natural.h()->is_valid());
}

TEST(NaturalToWireConversion, MoveOnlyUnion) {
  fidl::Arena arena;
  test_types::TestXUnion natural = test_types::TestXUnion::WithH(zx::handle());
  test_types::wire::TestXUnion wire = fidl::ToWire(arena, std::move(natural));
  EXPECT_FALSE(wire.h().is_valid());
}

TEST(WireToNaturalConversion, MoveOnlyTable) {
  zx::event ev;
  ASSERT_EQ(ZX_OK, zx::event::create(0, &ev));
  zx_handle_t handle = ev.get();

  fidl::Arena arena;
  test_types::wire::TestHandleTable wire_table =
      test_types::wire::TestHandleTable::Builder(arena).hs({std::move(ev)}).Build();
  test_types::TestHandleTable table = fidl::ToNatural(wire_table);
  EXPECT_FALSE(wire_table.hs().h.is_valid());
  EXPECT_TRUE(table.hs()->h().is_valid());
  EXPECT_EQ(handle, table.hs()->h().get());
}

TEST(NaturalToWireConversion, MoveOnlyTable) {
  zx::event ev;
  ASSERT_EQ(ZX_OK, zx::event::create(0, &ev));
  zx_handle_t handle = ev.get();

  fidl::Arena arena;
  test_types::TestHandleTable natural_table = test_types::TestHandleTable({.hs = std::move(ev)});
  test_types::wire::TestHandleTable table = fidl::ToWire(arena, std::move(natural_table));
  EXPECT_TRUE(natural_table.hs().has_value());
  EXPECT_FALSE(natural_table.hs().value().h().is_valid());
  EXPECT_TRUE(table.has_hs());
  EXPECT_EQ(handle, table.hs().h.get());
}

TEST(WireToNaturalConversion, MoveOnlyTableNonInlinableField) {
  zx::event ev;
  ASSERT_EQ(ZX_OK, zx::event::create(0, &ev));
  zx_handle_t handle = ev.get();

  fidl::Arena arena;
  test_types::wire::TestHandleTableNonInlinableField wire_table =
      test_types::wire::TestHandleTableNonInlinableField::Builder(arena)
          .hs(test_types::wire::NonInlinableHandleStruct{.h = std::move(ev), .i = 100})
          .Build();
  test_types::TestHandleTableNonInlinableField table = fidl::ToNatural(wire_table);
  EXPECT_FALSE(wire_table.hs().h.is_valid());
  EXPECT_TRUE(table.hs()->h().is_valid());
  EXPECT_EQ(handle, table.hs()->h().get());
  EXPECT_EQ(100, table.hs()->i());
}

TEST(NaturalToWireConversion, MoveOnlyTableNonInlinableField) {
  zx::event ev;
  ASSERT_EQ(ZX_OK, zx::event::create(0, &ev));
  zx_handle_t handle = ev.get();

  fidl::Arena arena;
  test_types::TestHandleTableNonInlinableField natural_table =
      test_types::TestHandleTableNonInlinableField({.hs = {{std::move(ev), 100}}});
  test_types::wire::TestHandleTableNonInlinableField table =
      fidl::ToWire(arena, std::move(natural_table));
  EXPECT_TRUE(natural_table.hs().has_value());
  EXPECT_FALSE(natural_table.hs().value().h().is_valid());
  EXPECT_TRUE(table.has_hs());
  EXPECT_EQ(handle, table.hs().h.get());
  EXPECT_EQ(100, table.hs().i);
}

TEST(WireToNaturalConversion, Request) {
  fidl::Request<test_types::Baz::Foo> request =
      fidl::ToNatural(fidl::WireRequest<test_types::Baz::Foo>{{.bar = 123}});
  EXPECT_EQ(123, request.req().bar());
}

TEST(WireToNaturalConversion, Response) {
  fidl::Response<test_types::Baz::Foo> response =
      fidl::ToNatural(fidl::WireResponse<test_types::Baz::Foo>({.bar = 123}));
  EXPECT_EQ(123, response.res().bar());
}

TEST(NaturalToWireConversion, Response) {
  fidl::Arena arena;
  fidl::WireResponse<test_types::Baz::Foo> response =
      fidl::ToWire(arena, fidl::Response<test_types::Baz::Foo>(test_types::FooResponse(123)));
  EXPECT_EQ(123, response.res.bar);
}

TEST(WireToNaturalConversion, ResponseEmptyResultSuccess) {
  fidl::Response<test_types::ErrorSyntax::EmptyPayload> natural =
      fidl::ToNatural(fidl::WireResponse<test_types::ErrorSyntax::EmptyPayload>());
  ASSERT_TRUE(natural.is_ok());
}

TEST(NaturalToWireConversion, ResponseEmptyResultSuccess) {
  fidl::Arena arena;
  fidl::WireResponse<test_types::ErrorSyntax::EmptyPayload> wire =
      fidl::ToWire(arena, fidl::Response<test_types::ErrorSyntax::EmptyPayload>(::fit::ok()));
  ASSERT_TRUE(wire.result.is_response());
}

TEST(WireToNaturalConversion, ResponseEmptyResultError) {
  fidl::WireResponse<test_types::ErrorSyntax::EmptyPayload> wire(
      test_types::wire::ErrorSyntaxEmptyPayloadResult::WithErr(123));
  fidl::Response<test_types::ErrorSyntax::EmptyPayload> natural = fidl::ToNatural(wire);
  ASSERT_TRUE(natural.is_error());
  EXPECT_EQ(123, natural.error_value());
}

TEST(NaturalToWireConversion, ResponseEmptyResultError) {
  fidl::Arena arena;
  fidl::Response<test_types::ErrorSyntax::EmptyPayload> natural(::fit::error(123));
  fidl::WireResponse<test_types::ErrorSyntax::EmptyPayload> wire = fidl::ToWire(arena, natural);
  ASSERT_TRUE(wire.result.is_err());
  EXPECT_EQ(123, wire.result.err());
}

TEST(WireToNaturalConversion, ResponseResultSuccess) {
  fidl::WireResponse<test_types::ErrorSyntax::FooPayload> wire(
      test_types::wire::ErrorSyntaxFooPayloadResult::WithResponse({.bar = 123}));
  fidl::Response<test_types::ErrorSyntax::FooPayload> natural = fidl::ToNatural(wire);
  ASSERT_TRUE(natural.is_ok());
  EXPECT_EQ(123, natural.value().bar());
}

TEST(NaturalToWireConversion, ResponseResultSuccess) {
  fidl::Arena arena;
  fidl::Response<test_types::ErrorSyntax::FooPayload> natural(::fit::ok(123));
  fidl::WireResponse<test_types::ErrorSyntax::FooPayload> wire =
      fidl::ToWire(arena, std::move(natural));
  ASSERT_TRUE(wire.result.is_response());
  EXPECT_EQ(123, wire.result.response().bar);
}

TEST(WireToNaturalConversion, ResponseResultError) {
  fidl::WireResponse<test_types::ErrorSyntax::FooPayload> wire(
      test_types::wire::ErrorSyntaxFooPayloadResult::WithErr(123));
  fidl::Response<test_types::ErrorSyntax::FooPayload> natural = fidl::ToNatural(wire);
  ASSERT_TRUE(natural.is_error());
  EXPECT_EQ(123, natural.error_value());
}

TEST(NaturalToWireConversion, ResponseResultError) {
  fidl::Arena arena;
  fidl::Response<test_types::ErrorSyntax::FooPayload> natural(fit::error(123));
  fidl::WireResponse<test_types::ErrorSyntax::FooPayload> wire =
      fidl::ToWire(arena, std::move(natural));
  ASSERT_TRUE(wire.result.is_err());
  EXPECT_EQ(123, wire.result.err());
}

TEST(WireToNaturalConversion, Event) {
  fidl::Event<test_types::Baz::FooEvent> event =
      fidl::ToNatural(fidl::WireEvent<test_types::Baz::FooEvent>(123));
  EXPECT_EQ(123, event.bar());
}

TEST(NaturalToWireConversion, Event) {
  fidl::Arena arena;
  fidl::WireEvent<test_types::Baz::FooEvent> event =
      fidl::ToWire(arena, fidl::Event<test_types::Baz::FooEvent>(123));
  EXPECT_EQ(123, event.bar);
}
#endif
