// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/test.types/cpp/natural_ostream.h>
#include <lib/fidl/cpp/wire/channel.h>
#include <lib/zx/vmo.h>

#include <sstream>

#include <gtest/gtest.h>

// Use the << operator to print a value to a std::string.
template <typename T>
std::string to_string(const T& value) {
  std::ostringstream buf;
  buf << value;
  return buf.str();
}

// Format a value as it would be formatted as the member of a FIDL type.
template <typename T>
std::string fidl_string(const T& value) {
  return to_string(fidl::ostream::Formatted<T>(value));
}

TEST(NaturalOStream, Primitive) {
  EXPECT_EQ(fidl_string<uint8_t>(42), "42");
  EXPECT_EQ(fidl_string<uint16_t>(42), "42");
  EXPECT_EQ(fidl_string<uint32_t>(42), "42");
  EXPECT_EQ(fidl_string<uint64_t>(42), "42");
  EXPECT_EQ(fidl_string<int8_t>(42), "42");
  EXPECT_EQ(fidl_string<int16_t>(42), "42");
  EXPECT_EQ(fidl_string<int32_t>(42), "42");
  EXPECT_EQ(fidl_string<int64_t>(42), "42");
  EXPECT_EQ(fidl_string<int8_t>(-42), "-42");
  EXPECT_EQ(fidl_string<int16_t>(-42), "-42");
  EXPECT_EQ(fidl_string<int32_t>(-42), "-42");
  EXPECT_EQ(fidl_string<int64_t>(-42), "-42");

  EXPECT_EQ(fidl_string<bool>(false), "false");
  EXPECT_EQ(fidl_string<bool>(true), "true");

  EXPECT_EQ(fidl_string<float>(3.14F), "3.14");
  EXPECT_EQ(fidl_string<double>(3.14), "3.14");
}

TEST(NaturalOStream, String) {
  EXPECT_EQ(fidl_string<std::string>("Hello"), "\"Hello\"");
  EXPECT_EQ(fidl_string<std::optional<std::string>>("Hello"), "\"Hello\"");
  EXPECT_EQ(fidl_string<std::optional<std::string>>(std::nullopt), "null");
  EXPECT_EQ(fidl_string<std::string>("Hello\nWorld"), "\"Hello\\x0aWorld\"");
  EXPECT_EQ(fidl_string<std::string>("Hello 🌏"), "\"Hello \\xf0\\x9f\\x8c\\x8f\"");
}

TEST(NaturalOStream, Vector) {
  EXPECT_EQ(fidl_string<std::vector<uint8_t>>({2, 4, 6, 8}), "[ 2, 4, 6, 8, ]");
  EXPECT_EQ(fidl_string<std::optional<std::vector<uint8_t>>>({{2, 4, 6, 8}}), "[ 2, 4, 6, 8, ]");
  EXPECT_EQ(fidl_string<std::optional<std::vector<uint8_t>>>(std::nullopt), "null");
  EXPECT_EQ(fidl_string<std::vector<bool>>({true, false}), "[ true, false, ]");
}

TEST(NaturalOStream, Array) {
  std::array<uint8_t, 4> numbers{2, 4, 6, 8};
  EXPECT_EQ(fidl_string(numbers), "[ 2, 4, 6, 8, ]");
  std::array<bool, 2> bools{true, false};
  EXPECT_EQ(fidl_string(bools), "[ true, false, ]");
}

TEST(NaturalOStream, Handle) {
  char buf[128];

  zx::vmo vmo_handle;
  ASSERT_EQ(zx::vmo::create(1024, 0, &vmo_handle), ZX_OK);
  snprintf(buf, 128, "vmo(%u)", vmo_handle.get());
  EXPECT_EQ(fidl_string(vmo_handle), buf);

  zx::event event_handle;
  ASSERT_EQ(zx::event::create(0, &event_handle), ZX_OK);
  snprintf(buf, 128, "event(%u)", event_handle.get());
  EXPECT_EQ(fidl_string(event_handle), buf);

  zx::channel channel_handle1, channel_handle2;
  ASSERT_EQ(zx::channel::create(0, &channel_handle1, &channel_handle2), ZX_OK);
  snprintf(buf, 128, "channel(%u)", channel_handle1.get());
  EXPECT_EQ(fidl_string(channel_handle1), buf);
  snprintf(buf, 128, "channel(%u)", channel_handle2.get());
  EXPECT_EQ(fidl_string(channel_handle2), buf);

  EXPECT_EQ(fidl_string(zx::handle()), "handle(0)");
}

TEST(NaturalOStream, StrictBits) {
  EXPECT_EQ(to_string(test_types::StrictBits::kB), "test_types::StrictBits(kB)");
  EXPECT_EQ(to_string(test_types::StrictBits::kB | test_types::StrictBits::kD),
            "test_types::StrictBits(kB|kD)");
  EXPECT_EQ(to_string(test_types::StrictBits::kB | test_types::StrictBits(128)),
            "test_types::StrictBits(kB)");
  EXPECT_EQ(to_string(test_types::StrictBits(128)), "test_types::StrictBits()");
}

TEST(NaturalOStream, FlexibleBits) {
  EXPECT_EQ(to_string(test_types::FlexibleBits::kB), "test_types::FlexibleBits(kB)");
  EXPECT_EQ(to_string(test_types::FlexibleBits::kB | test_types::FlexibleBits::kD),
            "test_types::FlexibleBits(kB|kD)");
  EXPECT_EQ(to_string(test_types::FlexibleBits::kB | test_types::FlexibleBits(128)),
            "test_types::FlexibleBits(kB|128)");
  EXPECT_EQ(to_string(test_types::FlexibleBits(128)), "test_types::FlexibleBits(128)");
}

TEST(NaturalOStream, StructEnum) {
  EXPECT_EQ(to_string(test_types::StrictEnum::kB), "test_types::StrictEnum::kB");
  EXPECT_EQ(to_string(test_types::StrictEnum::kD), "test_types::StrictEnum::kD");
}

TEST(NaturalOStream, FlexibleEnum) {
  EXPECT_EQ(to_string(test_types::FlexibleEnum::kB), "test_types::FlexibleEnum::kB");
  EXPECT_EQ(to_string(test_types::FlexibleEnum::kD), "test_types::FlexibleEnum::kD");
  EXPECT_EQ(to_string(test_types::FlexibleEnum(43)), "test_types::FlexibleEnum::UNKNOWN(43)");
}

TEST(NaturalOStream, Struct) {
  EXPECT_EQ(to_string(test_types::CopyableStruct{{.x = 42}}),
            "test_types::CopyableStruct{ x = 42, }");
  EXPECT_EQ(to_string(test_types::StructWithoutPadding({.a = 1, .b = 2, .c = 3, .d = 4})),
            "test_types::StructWithoutPadding{ a = 1, b = 2, c = 3, d = 4, }");
  EXPECT_EQ(to_string(test_types::VectorStruct({.v = {1, 2, 3, 4, 5, 6, 7}})),
            "test_types::VectorStruct{ v = [ 1, 2, 3, 4, 5, 6, 7, ], }");
}

TEST(NaturalOStream, Table) {
  EXPECT_EQ(to_string(test_types::TableMaxOrdinal3WithReserved2{}),
            "test_types::TableMaxOrdinal3WithReserved2{ }");
  EXPECT_EQ(to_string(test_types::TableMaxOrdinal3WithReserved2({.field_1 = 23, .field_3 = 42})),
            "test_types::TableMaxOrdinal3WithReserved2{ field_1 = 23, field_3 = 42, }");
}

TEST(NaturalOStream, Union) {
  EXPECT_EQ(to_string(test_types::TestUnion::WithPrimitive(42)),
            "test_types::TestUnion::primitive(42)");
  EXPECT_EQ(to_string(test_types::TestUnion::WithCopyable({{.x = 23}})),
            "test_types::TestUnion::copyable(test_types::CopyableStruct{ x = 23, })");
  EXPECT_EQ(
      to_string(test_types::TestXUnion(fidl::internal::DefaultConstructPossiblyInvalidObjectTag{})),
      "test_types::TestXUnion::Unknown");
}

TEST(NaturalOStream, Protocol) {
  auto endpoints = fidl::CreateEndpoints<test_types::TypesTest>();
  char buf[128];
  snprintf(buf, 128, "ClientEnd<test_types::TypesTest>(%u)", endpoints->client.channel().get());
  EXPECT_EQ(to_string(endpoints->client), buf);
  snprintf(buf, 128, "ServerEnd<test_types::TypesTest>(%u)", endpoints->server.channel().get());
  EXPECT_EQ(to_string(endpoints->server), buf);
}
