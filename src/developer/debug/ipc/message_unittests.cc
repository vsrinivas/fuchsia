// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

#include "src/developer/debug/ipc/message_reader.h"
#include "src/developer/debug/ipc/message_writer.h"

namespace debug_ipc {

TEST(Message, ReadWriteBytes) {
  constexpr uint32_t byte_count = 12;
  char bytes[byte_count] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};

  MessageWriter writer;
  writer.SerializeBytes(bytes, byte_count);
  writer.SerializeBytes(bytes, byte_count);

  std::vector<char> output = writer.MessageComplete();
  EXPECT_EQ(byte_count * 2, output.size());

  // First 4 bytes should encode the size (little-endian).
  EXPECT_EQ(byte_count * 2, static_cast<uint32_t>(output[0]));
  EXPECT_EQ(0, output[1]);
  EXPECT_EQ(0, output[2]);
  EXPECT_EQ(0, output[3]);

  // Remaining bytes should be identical to their indices mod the array size
  // (since it was
  // written twice).
  for (size_t i = 4; i < output.size(); i++) {
    EXPECT_EQ(i % byte_count, static_cast<uint32_t>(output[i]));
  }

  MessageReader reader(std::move(output));
  uint32_t read_size = 0;
  reader | read_size;
  ASSERT_FALSE(reader.has_error());
  EXPECT_EQ(byte_count * 2, read_size);

  char read_first[byte_count - 4];
  reader.SerializeBytes(read_first, byte_count - 4);
  ASSERT_FALSE(reader.has_error());
  EXPECT_EQ(4, read_first[0]);
  EXPECT_EQ(5, read_first[1]);
  EXPECT_EQ(6, read_first[2]);
  EXPECT_EQ(7, read_first[3]);

  char read_second[byte_count];
  reader.SerializeBytes(read_second, byte_count);
  ASSERT_FALSE(reader.has_error());
  for (uint32_t i = 0; i < byte_count; i++) {
    EXPECT_EQ(i, static_cast<uint32_t>(read_second[i]));
  }

  // Reading one more byte should fail.
  char one_more;
  reader | one_more;
  EXPECT_TRUE(reader.has_error());
}

TEST(Message, ReadWriteNumbers) {
  MessageWriter writer;
  uint32_t size = 0;  // Message size header.
  writer | size;

  int64_t expected_int64 = -7;
  uint64_t expected_uint64 = static_cast<uint64_t>(-8);

  writer | expected_int64;
  writer | expected_uint64;

  std::vector<char> message = writer.MessageComplete();
  constexpr uint32_t expected_message_size = 20;
  ASSERT_EQ(expected_message_size, message.size());

  MessageReader reader(std::move(message));

  // Message size header.
  uint32_t read_message_size = 0;
  reader | read_message_size;
  EXPECT_FALSE(reader.has_error());
  EXPECT_EQ(expected_message_size, read_message_size);

  int64_t read_int64 = 0;
  reader | read_int64;
  EXPECT_FALSE(reader.has_error());
  EXPECT_EQ(expected_int64, read_int64);

  uint64_t read_uint64 = 0;
  reader | read_uint64;
  EXPECT_FALSE(reader.has_error());
  EXPECT_EQ(expected_uint64, read_uint64);

  // Reading one more byte should fail.
  int64_t one_more;
  reader | one_more;
  EXPECT_TRUE(reader.has_error());
}

TEST(Message, ReadWriteOptional) {
  MessageWriter writer;
  uint32_t size = 0;  // Message size header.
  writer | size;

  std::optional<uint64_t> initial;
  writer | initial;

  initial.emplace(42);
  writer | initial;

  std::vector<char> message = writer.MessageComplete();
  constexpr uint32_t expected_message_size = 20;
  ASSERT_EQ(expected_message_size, message.size());

  MessageReader reader(std::move(message));

  // Message size header.
  uint32_t read_message_size = 0;
  reader | read_message_size;
  EXPECT_FALSE(reader.has_error());
  EXPECT_EQ(expected_message_size, read_message_size);

  std::optional<uint64_t> read;
  reader | read;
  EXPECT_FALSE(reader.has_error());
  EXPECT_FALSE(read.has_value());

  reader | read;
  EXPECT_FALSE(reader.has_error());
  EXPECT_EQ(read.value(), 42ull);

  // Reading one more byte should fail.
  int64_t one_more;
  reader | one_more;
  EXPECT_TRUE(reader.has_error());
}

}  // namespace debug_ipc
