// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file contains manual test cases that should be migrated to GIDL
// and be generated as part of conformance_test.cc in the future.

#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include <fidl/test/misc/llcpp/fidl.h>
#include <gtest/gtest.h>

#include "src/lib/fidl/llcpp/tests/test_utils.h"

namespace llcpp_misc = ::llcpp::fidl::test::misc;

TEST(InlineXUnionInStruct, Success) {
  // clang-format off
  const auto expected = std::vector<uint8_t>{
      0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // length of "before"
      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,  // "before" is present
      0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // xunion header
      0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // num bytes; num handles
      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,  // envelope data present
      0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // length of "after"
      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,  // "after" is present
      'b',  'e',  'f',  'o',  'r',  'e',               // "before" string
      0x00, 0x00,                                      // 2 bytes of padding
      0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // xunion header
      0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // num bytes; num handles
      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,  // envelope data present
      0xef, 0xbe, 0xad, 0xde, 0x00, 0x00, 0x00, 0x00,  // envelope data
      'a',  'f',  't',  'e',  'r',                     // "after" string
      0x00, 0x00, 0x00,                                // 3 bytes of padding
  };
  // clang-format on
  std::string before("before");
  std::string after("after");
  // encode
  {
    llcpp_misc::InlineXUnionInStruct input;
    llcpp_misc::SimpleUnion simple_union;
    int64_t i64 = 0xdeadbeef;
    simple_union.set_i64(fidl::unowned_ptr(&i64));
    input.before = fidl::unowned_str(before);
    input.xu.set_su(fidl::unowned_ptr(&simple_union));
    input.after = fidl::unowned_str(after);
    fidl::OwnedOutgoingMessage<llcpp_misc::InlineXUnionInStruct> encoded(&input);
    ASSERT_STREQ(encoded.error(), nullptr);
    ASSERT_TRUE(encoded.ok());
    EXPECT_TRUE(llcpp_conformance_utils::ComparePayload(encoded.GetOutgoingMessage().bytes(),
                                                        encoded.GetOutgoingMessage().byte_actual(),
                                                        &expected[0], expected.size()));
  }
  // decode
  {
    std::vector<uint8_t> encoded_bytes = expected;
    fidl::IncomingMessage<llcpp_misc::InlineXUnionInStruct> decoded(
        encoded_bytes.data(), static_cast<uint32_t>(encoded_bytes.size()));
    ASSERT_STREQ(decoded.error(), nullptr);
    ASSERT_TRUE(decoded.ok());
    const llcpp_misc::InlineXUnionInStruct& msg = *decoded.PrimaryObject();
    ASSERT_STREQ(msg.before.begin(), &before[0]);
    ASSERT_EQ(msg.before.size(), before.size());
    ASSERT_STREQ(msg.after.begin(), &after[0]);
    ASSERT_EQ(msg.after.size(), after.size());
    ASSERT_EQ(msg.xu.which(), llcpp_misc::SampleXUnion::Tag::kSu);
    const llcpp_misc::SimpleUnion& su = msg.xu.su();
    ASSERT_EQ(su.which(), llcpp_misc::SimpleUnion::Tag::kI64);
    ASSERT_EQ(su.i64(), 0xdeadbeef);
  }
}
TEST(PrimitiveInXUnionInStruct, Success) {
  // clang-format off
  const auto expected = std::vector<uint8_t>{
      0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // length of "before"
      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,  // "before" is present
      0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // xunion header
      0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // num bytes; num handles
      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,  // envelope data present
      0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // length of "after"
      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,  // "after" is present
      'b',  'e',  'f',  'o',  'r',  'e',               // "before" string
      0x00, 0x00,                                      // 2 bytes of padding
      0xef, 0xbe, 0xad, 0xde, 0x00, 0x00, 0x00, 0x00,  // envelope content
      'a',  'f',  't',  'e',  'r',                     // "after" string
      0x00, 0x00, 0x00,                                // 3 bytes of padding
  };
  // clang-format on
  std::string before("before");
  std::string after("after");
  int32_t integer = 0xdeadbeef;
  // encode
  {
    llcpp_misc::InlineXUnionInStruct input;
    input.before = fidl::unowned_str(before);
    input.xu.set_i(fidl::unowned_ptr(&integer));
    input.after = fidl::unowned_str(after);
    fidl::OwnedOutgoingMessage<llcpp_misc::InlineXUnionInStruct> encoded(&input);
    ASSERT_STREQ(encoded.error(), nullptr);
    ASSERT_TRUE(encoded.ok());
    EXPECT_TRUE(llcpp_conformance_utils::ComparePayload(encoded.GetOutgoingMessage().bytes(),
                                                        encoded.GetOutgoingMessage().byte_actual(),
                                                        &expected[0], expected.size()));
  }
  // decode
  {
    std::vector<uint8_t> encoded_bytes = expected;
    fidl::IncomingMessage<llcpp_misc::InlineXUnionInStruct> decoded(
        encoded_bytes.data(), static_cast<uint32_t>(encoded_bytes.size()), nullptr, 0);
    ASSERT_STREQ(decoded.error(), nullptr);
    ASSERT_TRUE(decoded.ok());
    const llcpp_misc::InlineXUnionInStruct& msg = *decoded.PrimaryObject();
    ASSERT_STREQ(msg.before.begin(), &before[0]);
    ASSERT_EQ(msg.before.size(), before.size());
    ASSERT_STREQ(msg.after.begin(), &after[0]);
    ASSERT_EQ(msg.after.size(), after.size());
    ASSERT_EQ(msg.xu.which(), llcpp_misc::SampleXUnion::Tag::kI);
    const int32_t& i = msg.xu.i();
    ASSERT_EQ(i, integer);
  }
}
TEST(InlineXUnionInStruct, FailToEncodeAbsentXUnion) {
  llcpp_misc::InlineXUnionInStruct input = {};
  std::string empty_str = "";
  input.before = fidl::unowned_str(empty_str);
  input.after = fidl::unowned_str(empty_str);
  fidl::OwnedOutgoingMessage<llcpp_misc::InlineXUnionInStruct> encoded(&input);
  EXPECT_STREQ(encoded.error(), "non-nullable xunion is absent");
  EXPECT_EQ(encoded.status(), ZX_ERR_INVALID_ARGS);
}
TEST(InlineXUnionInStruct, FailToDecodeAbsentXUnion) {
  // clang-format off
  std::vector<uint8_t> encoded_bytes = std::vector<uint8_t>{
      0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // length of "before"
      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,  // "before" is present
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // null xunion header
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // num bytes; num handles
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // envelope data absent
      0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // length of "after"
      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,  // "after" is present
      'b',  'e',  'f',  'o',  'r',  'e',               // "before" string
      0x00, 0x00,                                      // 2 bytes of padding
      'a',  'f',  't',  'e',  'r',                     // "after" string
      0x00, 0x00, 0x00,                                // 3 bytes of padding
  };
  // clang-format on
  fidl::IncomingMessage<llcpp_misc::InlineXUnionInStruct> decoded(
      encoded_bytes.data(), static_cast<uint32_t>(encoded_bytes.size()), nullptr, 0);
  EXPECT_STREQ(decoded.error(), "non-nullable xunion is absent");
  EXPECT_EQ(decoded.status(), ZX_ERR_INVALID_ARGS);
}
TEST(InlineXUnionInStruct, FailToDecodeZeroOrdinalXUnion) {
  // clang-format off
  std::vector<uint8_t> encoded_bytes = std::vector<uint8_t>{
      0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // length of "before"
      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,  // "before" is present
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // null xunion header
      0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // num bytes; num handles
      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,  // envelope data present
      0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // length of "after"
      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,  // "after" is present
      'b',  'e',  'f',  'o',  'r',  'e',               // "before" string
      0x00, 0x00,                                      // 2 bytes of padding
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // envelope content
      'a',  'f',  't',  'e',  'r',                     // "after" string
      0x00, 0x00, 0x00,                                // 3 bytes of padding
  };
  // clang-format on
  fidl::IncomingMessage<llcpp_misc::InlineXUnionInStruct> decoded(
      encoded_bytes.data(), static_cast<uint32_t>(encoded_bytes.size()), nullptr, 0);
  EXPECT_STREQ(decoded.error(), "xunion with zero as ordinal must be empty");
  EXPECT_EQ(decoded.status(), ZX_ERR_INVALID_ARGS);
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
      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,  // envelope data present
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
  fidl::IncomingMessage<llcpp_misc::InlineXUnionInStruct> decoded(
      encoded_bytes.data(), static_cast<uint32_t>(encoded_bytes.size()), nullptr, 0);
  ASSERT_TRUE(decoded.ok());
}
TEST(ComplexTable, SuccessEmpty) {
  // clang-format off
  const auto expected = std::vector<uint8_t>{
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // number of envelopes in ComplexTable
      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,  // envelopes data pointer is present
  };
  // clang-format on
  // encode
  {
    llcpp_misc::ComplexTable::UnownedBuilder builder;
    auto input = builder.build();
    fidl::OwnedOutgoingMessage<llcpp_misc::ComplexTable> encoded(&input);
    ASSERT_STREQ(encoded.error(), nullptr);
    ASSERT_TRUE(encoded.ok());
    EXPECT_TRUE(llcpp_conformance_utils::ComparePayload(encoded.GetOutgoingMessage().bytes(),
                                                        encoded.GetOutgoingMessage().byte_actual(),
                                                        &expected[0], expected.size()));
  }
  // decode
  {
    std::vector<uint8_t> encoded_bytes = expected;
    fidl::IncomingMessage<llcpp_misc::ComplexTable> decoded(
        encoded_bytes.data(), static_cast<uint32_t>(encoded_bytes.size()), nullptr, 0);
    ASSERT_STREQ(decoded.error(), nullptr);
    ASSERT_TRUE(decoded.ok());
    const llcpp_misc::ComplexTable& msg = *decoded.PrimaryObject();
    ASSERT_FALSE(msg.has_simple());
    ASSERT_FALSE(msg.has_u());
    ASSERT_FALSE(msg.has_strings());
  }
}
TEST(ComplexTable, FailToDecodeAbsentTable) {
  // clang-format off
  auto encoded_bytes = std::vector<uint8_t>{
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // number of envelopes in ComplexTable
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // envelopes data pointer is absent
  };
  // clang-format on
  fidl::IncomingMessage<llcpp_misc::ComplexTable> decoded(
      encoded_bytes.data(), static_cast<uint32_t>(encoded_bytes.size()), nullptr, 0);
  ASSERT_STREQ(decoded.error(), "absent pointer disallowed in non-nullable collection");
  ASSERT_EQ(decoded.status(), ZX_ERR_INVALID_ARGS);
}
TEST(ComplexTable, Success) {
  // clang-format off
  const auto expected = std::vector<uint8_t>{
      0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // number of envelopes in ComplexTable
      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,  // envelopes data pointer is present
      0x70, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // #1: num bytes; num handles
      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,  // #1: envelope data present
      0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // #2: num bytes; num handles
      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,  // #2: envelope data present
      0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // #3: num bytes; num handles
      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,  // #3: envelope data present
      // SimpleTable
      0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // number of envelopes in SimpleTable
      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,  // envelopes data pointer is present
      0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // #1: num bytes; num handles
      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,  // #1: envelope data present
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // #2: num bytes; num handles
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // #2: envelope data absent
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // #3: num bytes; num handles
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // #3: envelope data absent
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // #4: num bytes; num handles
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // #4: envelope data absent
      0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // #5: num bytes; num handles
      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,  // #5: envelope data present
      0x0d, 0xf0, 0xad, 0x8b, 0xcd, 0xab, 0xcd, 0xab,  // SimpleTable.x: 0xabcdabcd8badf00d
      0xd1, 0xf1, 0xd1, 0xf1, 0x78, 0x56, 0x34, 0x12,  // SimpleTable.y: 0x12345678f1d1f1d1
      // SampleXUnion
      0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // xunion header
      0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // num bytes; num handles
      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,  // envelope data present
      0xef, 0xbe, 0xad, 0xde, 0x00, 0x00, 0x00, 0x00,  // SampleXUnion.i: 0xdeadbeef
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
    auto simple_builder = llcpp_misc::SimpleTable::UnownedBuilder()
                              .set_x(fidl::unowned_ptr(&table_x))
                              .set_y(fidl::unowned_ptr(&table_y));
    auto simple_table = simple_builder.build();
    llcpp_misc::SampleXUnion xu;
    xu.set_i(fidl::unowned_ptr(&xunion_i));
    fidl::StringView strings_vector[]{
        fidl::unowned_str(before),
        fidl::unowned_str(after),
    };
    fidl::VectorView<fidl::StringView> strings = fidl::unowned_vec(strings_vector);
    auto builder = llcpp_misc::ComplexTable::UnownedBuilder()
                       .set_simple(fidl::unowned_ptr(&simple_table))
                       .set_u(fidl::unowned_ptr(&xu))
                       .set_strings(fidl::unowned_ptr(&strings));
    auto input = builder.build();
    fidl::OwnedOutgoingMessage<llcpp_misc::ComplexTable> encoded(&input);
    ASSERT_STREQ(encoded.error(), nullptr);
    ASSERT_TRUE(encoded.ok());
    EXPECT_TRUE(llcpp_conformance_utils::ComparePayload(encoded.GetOutgoingMessage().bytes(),
                                                        encoded.GetOutgoingMessage().byte_actual(),
                                                        &expected[0], expected.size()));
  }
  // decode
  {
    std::vector<uint8_t> encoded_bytes = expected;
    fidl::IncomingMessage<llcpp_misc::ComplexTable> decoded(
        encoded_bytes.data(), static_cast<uint32_t>(encoded_bytes.size()), nullptr, 0);
    ASSERT_STREQ(decoded.error(), nullptr);
    ASSERT_TRUE(decoded.ok());
    const llcpp_misc::ComplexTable& msg = *decoded.PrimaryObject();
    ASSERT_TRUE(msg.has_simple());
    ASSERT_TRUE(msg.simple().has_x());
    ASSERT_EQ(msg.simple().x(), table_x);
    ASSERT_TRUE(msg.simple().has_y());
    ASSERT_EQ(msg.simple().y(), table_y);
    ASSERT_TRUE(msg.has_u());
    ASSERT_EQ(msg.u().which(), llcpp_misc::SampleXUnion::Tag::kI);
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
