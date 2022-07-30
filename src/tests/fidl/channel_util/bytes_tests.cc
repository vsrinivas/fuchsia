// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <random>

#include <gtest/gtest.h>

#include "src/tests/fidl/channel_util/bytes.h"

namespace channel_util {

TEST(Bytes, Basic) {
  Bytes b1(0x12);
  ASSERT_EQ(1u, b1.size());
  ASSERT_EQ(0x12, b1.data()[0]);

  Bytes b2 = {1, 2, 3, 4};
  ASSERT_EQ(4u, b2.size());
  ASSERT_EQ(1, b2.data()[0]);
  ASSERT_EQ(2, b2.data()[1]);
  ASSERT_EQ(3, b2.data()[2]);
  ASSERT_EQ(4, b2.data()[3]);
}

TEST(Bytes, AsBytes) {
  struct X {
    uint32_t a;
    uint32_t b;
  };
  X x = {
      .a = 123,
      .b = 200,
  };

  Bytes expected = {123, 0, 0, 0, 200, 0, 0, 0};
  EXPECT_EQ(expected, as_bytes(x));
}

namespace {
template <typename T>
T generate_number() {
  if constexpr (sizeof(T) > 1) {
    std::mt19937 generator(1);
    std::uniform_int_distribution<T> random;
    return random(generator);
  } else {
    return 123;
  }
}

template <typename T>
void test_number(Bytes (*fn)(T)) {
  T value = generate_number<T>();
  Bytes b = fn(value);
  ASSERT_EQ(sizeof(T), b.size());
  for (size_t i = 0; i < b.size(); i++) {
    ASSERT_EQ(reinterpret_cast<uint8_t*>(&value)[i], b.data()[i]);
  }
}
}  // namespace

TEST(Bytes, Numbers) {
  test_number(u8);
  test_number(u16);
  test_number(u32);
  test_number(u64);
  test_number(i8);
  test_number(i16);
  test_number(i32);
  test_number(i64);
}

TEST(Bytes, Repeat) {
  Bytes expected = {123, 123, 123};
  ASSERT_EQ(expected, repeat(123).times(3));
}

TEST(Bytes, Padding) {
  Bytes expected = {0x00, 0x00, 0x00};
  ASSERT_EQ(expected, padding(3));
}

TEST(Bytes, Header) {
  Bytes expected = as_bytes(fidl_message_header_t{
      .txid = 123,
      .at_rest_flags = {FIDL_MESSAGE_HEADER_AT_REST_FLAGS_0_USE_VERSION_V2, 0},
      .dynamic_flags = FIDL_MESSAGE_HEADER_DYNAMIC_FLAGS_STRICT_METHOD,
      .magic_number = kFidlWireFormatMagicNumberInitial,
      .ordinal = 456,
  });
  ASSERT_EQ(expected, header(123, 456, fidl::MessageDynamicFlags::kStrictMethod));
}

TEST(Bytes, UnionOrdinal) {
  Bytes expected = {0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
  ASSERT_EQ(expected, union_ordinal(3));
}

TEST(Bytes, TableMaxOrdinal) {
  Bytes expected = {0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
  ASSERT_EQ(expected, table_max_ordinal(3));
}

TEST(Bytes, StringLength) {
  Bytes expected = {0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
  ASSERT_EQ(expected, string_length(3));
}

TEST(Bytes, VectorLength) {
  Bytes expected = {0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
  ASSERT_EQ(expected, vector_length(3));
}

TEST(Bytes, StringHeader) {
  Bytes expected = {
      0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
  };
  ASSERT_EQ(expected, string_header(3));
}

TEST(Bytes, VectorHeader) {
  Bytes expected = {0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
  ASSERT_EQ(expected, vector_header(3));
}

TEST(Bytes, HandlePresent) {
  Bytes expected = {0xff, 0xff, 0xff, 0xff};
  ASSERT_EQ(expected, handle_present());
}
TEST(Bytes, HandleAbsent) {
  Bytes expected = {0x00, 0x00, 0x00, 0x00};
  ASSERT_EQ(expected, handle_absent());
}

TEST(Bytes, PointerPresent) {
  Bytes expected = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
  ASSERT_EQ(expected, pointer_present());
}
TEST(Bytes, PointerAbsent) {
  Bytes expected = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
  ASSERT_EQ(expected, pointer_absent());
}

TEST(Bytes, OutOfLineEnvelope) {
  Bytes expected = {24, 0, 0, 0, 2, 0, 0, 0};
  ASSERT_EQ(expected, out_of_line_envelope(24, 2));
}

TEST(Bytes, InlineEnvelope) {
  Bytes expected = {0xfe, 0xdc, 0xba, 0x98, 0x01, 0x00, 0x01, 0x00};
  ASSERT_EQ(expected, inline_envelope({0xfe, 0xdc, 0xba, 0x98}, true));

  Bytes expected2 = {0x55, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00};
  ASSERT_EQ(expected2, inline_envelope(u8(0x55), false));
}

}  // namespace channel_util
