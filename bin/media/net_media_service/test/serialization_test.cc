// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/media/src/net/serialization.h"

#include "gtest/gtest.h"

namespace media {
namespace {

// Tests that a Serializer behaves as expected in initial state.
TEST(SerializationTest, SerializerInitialState) {
  Serializer under_test;
  std::vector<uint8_t> initial_serial_message = under_test.GetSerialMessage();
  EXPECT_EQ(0u, initial_serial_message.size());
}

// Tests that a Deserializer behaves as expected in initial state.
TEST(SerializationTest, DeserializerInitialState) {
  std::vector<uint8_t> serial_message(1);
  Deserializer under_test(serial_message);
  EXPECT_TRUE(under_test.healthy());
  EXPECT_FALSE(under_test.complete());
  EXPECT_NE(nullptr, under_test.Bytes(1));
  EXPECT_TRUE(under_test.complete());
  EXPECT_TRUE(under_test.GetBytes(0, nullptr));
  EXPECT_TRUE(under_test.complete());
}

// Tests that a Deserializer behaves as expected when unhealthy.
TEST(SerializationTest, DeserializerUnhealthy) {
  std::vector<uint8_t> serial_message(1);
  Deserializer under_test(serial_message);
  under_test.MarkUnhealthy();
  EXPECT_FALSE(under_test.healthy());
  EXPECT_FALSE(under_test.complete());
  EXPECT_EQ(nullptr, under_test.Bytes(0));
  EXPECT_FALSE(under_test.GetBytes(0, nullptr));
}

// Tests that a Deserializer behaves as expected when too much is read from it.
TEST(SerializationTest, DeserializerStarves) {
  std::vector<uint8_t> serial_message(1);
  Deserializer under_test(serial_message);
  EXPECT_TRUE(under_test.healthy());
  EXPECT_FALSE(under_test.complete());
  EXPECT_EQ(nullptr, under_test.Bytes(2));
  EXPECT_FALSE(under_test.healthy());
}

// Tests that values round-trip properly through serialization and
// deserialization.
TEST(SerializationTest, RoundTrip) {
  Serializer serializer_under_test;
  bool bool_in = true;
  uint8_t uint8_t_in = 0x12u;
  uint16_t uint16_t_in = 0x3456u;
  uint32_t uint32_t_in = 0x789abcdeu;
  uint64_t uint64_t_in = 0xf0123456789abcdeu;
  int8_t int8_t_in = -1;
  int16_t int16_t_in = -2000;
  int32_t int32_t_in = -4000000;
  int64_t int64_t_in = -8000000000000ll;
  std::string string_in = "Does it work?";
  std::string empty_string_in = "";

  serializer_under_test << bool_in << uint8_t_in << uint16_t_in << uint32_t_in
                        << uint64_t_in << int8_t_in << int16_t_in << int32_t_in
                        << int64_t_in << string_in << empty_string_in;

  Deserializer deserializer_under_test(
      serializer_under_test.GetSerialMessage());

  bool bool_out;
  uint8_t uint8_t_out;
  uint16_t uint16_t_out;
  uint32_t uint32_t_out;
  uint64_t uint64_t_out;
  int8_t int8_t_out;
  int16_t int16_t_out;
  int32_t int32_t_out;
  int64_t int64_t_out;
  std::string string_out;
  std::string empty_string_out;

  deserializer_under_test >> bool_out >> uint8_t_out >> uint16_t_out >>
      uint32_t_out >> uint64_t_out >> int8_t_out >> int16_t_out >>
      int32_t_out >> int64_t_out >> string_out >> empty_string_out;

  EXPECT_EQ(bool_in, bool_out);
  EXPECT_EQ(uint8_t_in, uint8_t_out);
  EXPECT_EQ(uint16_t_in, uint16_t_out);
  EXPECT_EQ(uint32_t_in, uint32_t_out);
  EXPECT_EQ(uint64_t_in, uint64_t_out);
  EXPECT_EQ(int8_t_in, int8_t_out);
  EXPECT_EQ(int16_t_in, int16_t_out);
  EXPECT_EQ(int32_t_in, int32_t_out);
  EXPECT_EQ(int64_t_in, int64_t_out);
  EXPECT_EQ(string_in, string_out);
  EXPECT_EQ(empty_string_in, empty_string_out);
}

}  // namespace
}  // namespace media
