// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file contains manual test cases that should be migrated to GIDL
// and be generated as part of conformance_test.cc in the future.
// Note that it exercises |fidl::Encode|, which is a wrapper over
// |OwnedEncodedMessage| et al, and exercises slightly different code
// paths, because it disables iovec.

#include <fidl/fidl.test.misc/cpp/wire_types.h>
#include <fidl/manual.conformance.large/cpp/wire.h>

#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "src/lib/fidl/llcpp/tests/conformance/conformance_utils.h"

namespace llcpp_misc = ::fidl_test_misc;

namespace {

const auto kV2Metadata =
    fidl::internal::WireFormatMetadataForVersion(fidl::internal::WireFormatVersion::kV2);

}

TEST(PrimitiveInXUnionInStruct, Success) {
  // clang-format off
  const auto expected = std::vector<uint8_t>{
      0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // length of "before"
      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,  // "before" is present
      0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // xunion header
      0xef, 0xbe, 0xad, 0xde, 0x00, 0x00, 0x01, 0x00,  // inline envelope content
      0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // length of "after"
      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,  // "after" is present
      'b',  'e',  'f',  'o',  'r',  'e',               // "before" string
      0x00, 0x00,                                      // 2 bytes of padding
      'a',  'f',  't',  'e',  'r',                     // "after" string
      0x00, 0x00, 0x00,                                // 3 bytes of padding
  };
  // clang-format on
  std::string before("before");
  std::string after("after");
  int32_t integer = 0xdeadbeef;
  // encode
  {
    llcpp_misc::wire::InlineXUnionInStruct input;
    input.before = fidl::StringView::FromExternal(before);
    input.xu = llcpp_misc::wire::SampleXUnion::WithI(integer);
    input.after = fidl::StringView::FromExternal(after);
    fidl::OwnedEncodeResult encoded = fidl::Encode(input);
    ASSERT_TRUE(encoded.message().ok());
    auto bytes = encoded.message().CopyBytes();
    EXPECT_TRUE(llcpp_conformance_utils::ComparePayload(bytes.data(), bytes.size(), &expected[0],
                                                        expected.size()));
    EXPECT_EQ(encoded.wire_format_metadata().ToOpaque().metadata, kV2Metadata.ToOpaque().metadata);
  }
  // decode
  {
    std::vector<uint8_t> encoded_bytes = expected;
    fit::result result = fidl::InplaceDecode<llcpp_misc::wire::InlineXUnionInStruct>(
        fidl::EncodedMessage::Create(encoded_bytes), kV2Metadata);
    ASSERT_TRUE(result.is_ok());
    const llcpp_misc::wire::InlineXUnionInStruct& msg = result.value().value();
    ASSERT_STREQ(msg.before.begin(), &before[0]);
    ASSERT_EQ(msg.before.size(), before.size());
    ASSERT_STREQ(msg.after.begin(), &after[0]);
    ASSERT_EQ(msg.after.size(), after.size());
    ASSERT_EQ(msg.xu.Which(), llcpp_misc::wire::SampleXUnion::Tag::kI);
    const int32_t& i = msg.xu.i();
    ASSERT_EQ(i, integer);
  }
}

TEST(PrimitiveInXUnion, Success) {
  // clang-format off
  const auto expected = std::vector<uint8_t>{
      0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // xunion header
      0xef, 0xbe, 0xad, 0xde, 0x00, 0x00, 0x01, 0x00,  // inline envelope content
  };
  // clang-format on
  int32_t integer = 0xdeadbeef;
  // encode
  {
    auto xu = llcpp_misc::wire::SampleXUnion::WithI(integer);
    fidl::OwnedEncodeResult encoded = fidl::Encode(xu);
    ASSERT_TRUE(encoded.message().ok());
    auto bytes = encoded.message().CopyBytes();
    EXPECT_TRUE(llcpp_conformance_utils::ComparePayload(bytes.data(), bytes.size(), &expected[0],
                                                        expected.size()));
    EXPECT_EQ(encoded.wire_format_metadata().ToOpaque().metadata, kV2Metadata.ToOpaque().metadata);
  }
  // decode
  {
    std::vector<uint8_t> encoded_bytes = expected;
    fit::result result = fidl::InplaceDecode<llcpp_misc::wire::SampleXUnion>(
        fidl::EncodedMessage::Create(encoded_bytes), kV2Metadata);
    ASSERT_TRUE(result.is_ok());
    const llcpp_misc::wire::SampleXUnion& xu = result.value().value();
    ASSERT_EQ(xu.Which(), llcpp_misc::wire::SampleXUnion::Tag::kI);
    const int32_t& i = xu.i();
    ASSERT_EQ(i, integer);
  }
}

TEST(InlineXUnionInStruct, FailToEncodeAbsentXUnion) {
  llcpp_misc::wire::InlineXUnionInStruct input = {};
  std::string empty_str = "";
  input.before = fidl::StringView::FromExternal(empty_str);
  input.after = fidl::StringView::FromExternal(empty_str);
  fidl::OwnedEncodeResult encoded = fidl::Encode(input);
  EXPECT_FALSE(encoded.message().ok());
  // TODO(fxbug.dev/35381): Test a reason enum instead of comparing strings.
  EXPECT_EQ(std::string(encoded.message().error().lossy_description()),
            "non-nullable union is absent");
  EXPECT_EQ(encoded.message().status(), ZX_ERR_INVALID_ARGS);
}

TEST(InlineXUnionInStruct, FailToDecodeAbsentXUnion) {
  // clang-format off
  std::vector<uint8_t> encoded_bytes = std::vector<uint8_t>{
      0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // length of "before"
      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,  // "before" is present
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // null xunion header
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // num bytes; num handles
      0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // length of "after"
      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,  // "after" is present
      'b',  'e',  'f',  'o',  'r',  'e',               // "before" string
      0x00, 0x00,                                      // 2 bytes of padding
      'a',  'f',  't',  'e',  'r',                     // "after" string
      0x00, 0x00, 0x00,                                // 3 bytes of padding
  };
  // clang-format on
  fit::result result = fidl::InplaceDecode<llcpp_misc::wire::InlineXUnionInStruct>(
      fidl::EncodedMessage::Create(encoded_bytes), kV2Metadata);
  EXPECT_FALSE(result.is_ok());
  // TODO(fxbug.dev/35381): Test a reason enum instead of comparing strings.
  EXPECT_EQ(std::string(result.error_value().lossy_description()), "non-nullable union is absent");
  EXPECT_EQ(result.error_value().status(), ZX_ERR_INVALID_ARGS);
}

TEST(InlineXUnionInStruct, FailToDecodeZeroOrdinalXUnion) {
  // clang-format off
  std::vector<uint8_t> encoded_bytes = std::vector<uint8_t>{
      0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // length of "before"
      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,  // "before" is present
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // null xunion header
      0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // num bytes; num handles
      0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // length of "after"
      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,  // "after" is present
      'b',  'e',  'f',  'o',  'r',  'e',               // "before" string
      0x00, 0x00,                                      // 2 bytes of padding
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // envelope content
      'a',  'f',  't',  'e',  'r',                     // "after" string
      0x00, 0x00, 0x00,                                // 3 bytes of padding
  };
  // clang-format on
  fit::result result = fidl::InplaceDecode<llcpp_misc::wire::InlineXUnionInStruct>(
      fidl::EncodedMessage::Create(encoded_bytes), kV2Metadata);
  EXPECT_FALSE(result.is_ok());
  // TODO(fxbug.dev/35381): Test a reason enum instead of comparing strings.
  EXPECT_EQ(std::string(result.error_value().lossy_description()), "non-nullable union is absent");
  EXPECT_EQ(result.error_value().status(), ZX_ERR_INVALID_ARGS);
}

// The xunion ordinal hashing algorithm generates 32 bit values. But if it did
// generate values bigger than that, they would decode successfully
TEST(InlineXUnionInStruct, SuccessLargeXUnionOrdinal) {
  // clang-format off
  auto encoded_bytes = std::vector<uint8_t>{
      0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // length of "before"
      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,  // "before" is present
      0x53, 0x76, 0x31, 0x6f, 0xaa, 0xaa, 0xaa, 0xaa,  // xunion header
      0x18, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // num bytes; num handles
      0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // length of "after"
      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,  // "after" is present
      'b', 'e', 'f', 'o', 'r', 'e',                    // "before" string
      0x00, 0x00,                                      // 2 bytes of padding
      0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // envelope content
      0xef, 0xbe, 0xad, 0xde, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      'a', 'f', 't', 'e', 'r',                         // "after" string
      0x00, 0x00, 0x00,                                // 3 bytes of padding
  };
  // clang-format on
  fit::result result = fidl::InplaceDecode<llcpp_misc::wire::InlineXUnionInStruct>(
      fidl::EncodedMessage::Create(encoded_bytes), kV2Metadata);
  ASSERT_TRUE(result.is_ok());
}

TEST(ComplexTable, Success) {
  // clang-format off
  const auto expected = std::vector<uint8_t>{
      0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // number of envelopes in ComplexTable
      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,  // envelopes data pointer is present
      0x48, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // #1: num bytes; num handles
      0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // #2: num bytes; num handles
      0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // #3: num bytes; num handles
      // SimpleTable
      0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // number of envelopes in SimpleTable
      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,  // envelopes data pointer is present
      0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // #1: num bytes; num handles
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // #2: num bytes; num handles
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // #3: num bytes; num handles
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // #4: num bytes; num handles
      0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // #5: num bytes; num handles
      0x0d, 0xf0, 0xad, 0x8b, 0xcd, 0xab, 0xcd, 0xab,  // SimpleTable.x: 0xabcdabcd8badf00d
      0xd1, 0xf1, 0xd1, 0xf1, 0x78, 0x56, 0x34, 0x12,  // SimpleTable.y: 0x12345678f1d1f1d1
      // SampleXUnion
      0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // xunion header
      0xef, 0xbe, 0xad, 0xde, 0x00, 0x00, 0x01, 0x00,  // SampleXUnion.i: 0xdeadbeef
      // vector<string>
      0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // length of string vector
      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,  // string vector data present
      0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // length of "before"
      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,  // "before" is present
      0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // length of "after"
      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,  // "after" is present
      'b',  'e',  'f',  'o',  'r',  'e',               // "before" string
      0x00, 0x00,                                      // 2 bytes of padding
      'a',  'f',  't',  'e',  'r',                     // "after" string
      0x00, 0x00, 0x00,                                // 3 bytes of padding
  };
  // clang-format on
  std::string before("before");
  std::string after("after");
  int64_t table_x = 0xabcdabcd8badf00d;
  int64_t table_y = 0x12345678f1d1f1d1;
  int32_t xunion_i = 0xdeadbeef;
  // encode
  {
    fidl::Arena allocator;
    llcpp_misc::wire::SimpleTable simple_table(allocator);
    simple_table.set_x(allocator, table_x).set_y(allocator, table_y);
    llcpp_misc::wire::SampleXUnion xu;
    xu = llcpp_misc::wire::SampleXUnion::WithI(xunion_i);
    fidl::StringView strings_vector[]{
        fidl::StringView::FromExternal(before),
        fidl::StringView::FromExternal(after),
    };
    auto strings = fidl::VectorView<fidl::StringView>::FromExternal(strings_vector);
    llcpp_misc::wire::ComplexTable input(allocator);
    input.set_simple(allocator, std::move(simple_table))
        .set_u(allocator, std::move(xu))
        .set_strings(allocator, std::move(strings));
    fidl::OwnedEncodeResult encoded = fidl::Encode(input);
    ASSERT_TRUE(encoded.message().ok());
    auto bytes = encoded.message().CopyBytes();
    EXPECT_TRUE(llcpp_conformance_utils::ComparePayload(bytes.data(), bytes.size(), &expected[0],
                                                        expected.size()));
  }
  // decode
  {
    std::vector<uint8_t> encoded_bytes = expected;
    fit::result result = fidl::InplaceDecode<llcpp_misc::wire::ComplexTable>(
        fidl::EncodedMessage::Create(encoded_bytes), kV2Metadata);
    ASSERT_TRUE(result.is_ok());
    const llcpp_misc::wire::ComplexTable& msg = result.value().value();
    ASSERT_TRUE(msg.has_simple());
    ASSERT_TRUE(msg.simple().has_x());
    ASSERT_EQ(msg.simple().x(), table_x);
    ASSERT_TRUE(msg.simple().has_y());
    ASSERT_EQ(msg.simple().y(), table_y);
    ASSERT_TRUE(msg.has_u());
    ASSERT_EQ(msg.u().Which(), llcpp_misc::wire::SampleXUnion::Tag::kI);
    const int32_t& i = msg.u().i();
    ASSERT_EQ(i, xunion_i);
    ASSERT_TRUE(msg.has_strings());
    ASSERT_EQ(msg.strings().count(), 2u);
    ASSERT_STREQ(msg.strings()[0].begin(), &before[0]);
    ASSERT_EQ(msg.strings()[0].size(), before.size());
    ASSERT_STREQ(msg.strings()[1].begin(), &after[0]);
    ASSERT_EQ(msg.strings()[1].size(), after.size());
  }
}

// TODO(fxbug.dev/82681): we should support large message encoding as part of
// FIDL-at-rest, at which point this test would be adjusted to check for
// success.
TEST(InputExceeds64KiB, EncodeUnsupported) {
  // We have observed crashes when an envelope header is the first object over
  // the 64 KiB boundary. It's difficult to place the envelope at exactly that
  // offset as we evolve through wire formats, hence this test tries through
  // a range of offsets.
  for (size_t filler_size = ZX_CHANNEL_MAX_MSG_BYTES - 100; filler_size < ZX_CHANNEL_MAX_MSG_BYTES;
       filler_size += 8) {
    fidl::Arena arena;
    manual_conformance_large::wire::LargeTable table(arena);
    table.set_filler(arena);
    table.filler().Allocate(arena, filler_size);
    table.set_overflow(arena, arena);
    table.overflow().set_placeholder(arena);
    static_assert(sizeof(std::remove_reference_t<decltype(table.overflow().placeholder())>) == 100,
                  "Need a reasonably sized last piece of data to make the whole message reliably "
                  "go over the 64 KiB limit.");

    fidl::OwnedEncodeResult encoded = fidl::Encode(table);
    EXPECT_FALSE(encoded.message().ok());
    EXPECT_EQ(encoded.message().status(), ZX_ERR_BUFFER_TOO_SMALL);
    EXPECT_STREQ(encoded.message().lossy_description(), "backing buffer size exceeded");
  }
}
