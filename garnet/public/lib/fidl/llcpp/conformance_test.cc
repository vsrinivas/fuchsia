// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include <fidl/test/misc/llcpp/fidl.h>
#include "gtest/gtest.h"

bool ComparePayload(const uint8_t* actual, size_t actual_size,
                    const uint8_t* expected, size_t expected_size) {
  bool pass = true;
  for (size_t i = 0; i < actual_size && i < expected_size; i++) {
    if (actual[i] != expected[i]) {
      pass = false;
      std::cout << std::dec << "element[" << i << "]: " << std::hex
          << "actual=0x" << +actual[i] << " "
          << "expected=0x" << +expected[i] << "\n";
    }
  }
  if (actual_size != expected_size) {
      pass = false;
      std::cout << std::dec << "element[...]: "
          << "actual.size=" << +actual_size << " "
          << "expected.size=" << +expected_size << "\n";
  }
  return pass;
}

TEST(InlineXUnionInStruct, Success) {
  const auto expected = std::vector<uint8_t>{
      0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // length of "before"
      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,  // "before" is present
      0x53, 0x76, 0x31, 0x6f, 0x00, 0x00, 0x00, 0x00,  // xunion header
      0x18, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // num bytes; num handles
      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,  // envelope data present
      0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // length of "after"
      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,  // "after" is present
      'b',  'e',  'f',  'o',  'r',  'e',               // "before" string
      0x00, 0x00,                                      // 2 bytes of padding
      0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // envelope content
      0xef, 0xbe, 0xad, 0xde, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      'a',  'f',  't',  'e',  'r',                     // "after" string
      0x00, 0x00, 0x00,                                // 3 bytes of padding
  };
  std::string before("before");
  std::string after("after");

  // encode
  {
    fidl::test::misc::InlineXUnionInStruct input;
    fidl::test::misc::SimpleUnion simple_union;
    simple_union.set_i64(0xdeadbeef);
    input.before = fidl::StringView(before.size(), &before[0]);
    input.xu.set_su(&simple_union);
    input.after = fidl::StringView(after.size(), &after[0]);

    std::vector<uint8_t> buffer(ZX_CHANNEL_MAX_MSG_BYTES);
    fidl::BytePart bytes(&buffer[0], static_cast<uint32_t>(buffer.size()));
    auto linearize_result = fidl::Linearize(&input, std::move(bytes));
    ASSERT_STREQ(linearize_result.error, nullptr);
    ASSERT_EQ(linearize_result.status, ZX_OK);

    auto encode_result = fidl::Encode(std::move(linearize_result.message));
    ASSERT_STREQ(encode_result.error, nullptr);
    ASSERT_EQ(encode_result.status, ZX_OK);

    EXPECT_TRUE(ComparePayload(encode_result.message.bytes().begin(),
                               encode_result.message.bytes().size(),
                               &expected[0], expected.size()));
  }

  // decode
  {
      std::vector<uint8_t> encoded_bytes = expected;
      fidl::EncodedMessage<fidl::test::misc::InlineXUnionInStruct> encoded_msg(
        fidl::BytePart(&encoded_bytes[0],
                       static_cast<uint32_t>(encoded_bytes.size()),
                       static_cast<uint32_t>(encoded_bytes.size())));
      auto decode_result = fidl::Decode(std::move(encoded_msg));
      ASSERT_STREQ(decode_result.error, nullptr);
      ASSERT_EQ(decode_result.status, ZX_OK);

      const fidl::test::misc::InlineXUnionInStruct& msg =
          *decode_result.message.message();
      ASSERT_STREQ(msg.before.begin(), &before[0]);
      ASSERT_EQ(msg.before.size(), before.size());
      ASSERT_STREQ(msg.after.begin(), &after[0]);
      ASSERT_EQ(msg.after.size(), after.size());
      ASSERT_EQ(msg.xu.which(), fidl::test::misc::SampleXUnion::Tag::kSu);
      const fidl::test::misc::SimpleUnion& su = msg.xu.su();
      ASSERT_EQ(su.which(), fidl::test::misc::SimpleUnion::Tag::kI64);
      ASSERT_EQ(su.i64(), 0xdeadbeef);
  }
}

TEST(PrimitiveInXUnionInStruct, Success) {
  const auto expected = std::vector<uint8_t>{
      0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // length of "before"
      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,  // "before" is present
      0xa5, 0x47, 0xdf, 0x29, 0x00, 0x00, 0x00, 0x00,  // xunion header
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
  std::string before("before");
  std::string after("after");
  int32_t integer = 0xdeadbeef;

  // encode
  {
    fidl::test::misc::InlineXUnionInStruct input;
    input.before = fidl::StringView(before.size(), &before[0]);
    input.xu.set_i(&integer);
    input.after = fidl::StringView(after.size(), &after[0]);

    std::vector<uint8_t> buffer(ZX_CHANNEL_MAX_MSG_BYTES);
    fidl::BytePart bytes(&buffer[0], static_cast<uint32_t>(buffer.size()));
    auto linearize_result = fidl::Linearize(&input, std::move(bytes));
    ASSERT_STREQ(linearize_result.error, nullptr);
    ASSERT_EQ(linearize_result.status, ZX_OK);

    auto encode_result = fidl::Encode(std::move(linearize_result.message));
    ASSERT_STREQ(encode_result.error, nullptr);
    ASSERT_EQ(encode_result.status, ZX_OK);

    EXPECT_TRUE(ComparePayload(encode_result.message.bytes().begin(),
                               encode_result.message.bytes().size(),
                               &expected[0], expected.size()));
  }

  // decode
  {
      std::vector<uint8_t> encoded_bytes = expected;
      fidl::EncodedMessage<fidl::test::misc::InlineXUnionInStruct> encoded_msg(
        fidl::BytePart(&encoded_bytes[0],
                       static_cast<uint32_t>(encoded_bytes.size()),
                       static_cast<uint32_t>(encoded_bytes.size())));
      auto decode_result = fidl::Decode(std::move(encoded_msg));
      ASSERT_STREQ(decode_result.error, nullptr);
      ASSERT_EQ(decode_result.status, ZX_OK);

      const fidl::test::misc::InlineXUnionInStruct& msg =
          *decode_result.message.message();
      ASSERT_STREQ(msg.before.begin(), &before[0]);
      ASSERT_EQ(msg.before.size(), before.size());
      ASSERT_STREQ(msg.after.begin(), &after[0]);
      ASSERT_EQ(msg.after.size(), after.size());
      ASSERT_EQ(msg.xu.which(), fidl::test::misc::SampleXUnion::Tag::kI);
      const int32_t& i = msg.xu.i();
      ASSERT_EQ(i, integer);
  }
}

