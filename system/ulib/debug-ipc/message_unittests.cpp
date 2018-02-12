// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "debug-ipc/message_reader.h"
#include "debug-ipc/message_writer.h"

// TODO(brettw) figure out testing.
#if 0

#include "testing/gtest/include/gtest/gtest.h"

TEST(Message, ReadWriteBytes) {
    constexpr uint64_t byte_count = 12;
    char bytes[byte_count] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 };

    MessageWriter writer;
    writer.WriteBytes(bytes, byte_count);
    writer.WriteBytes(bytes, byte_count);

    uint64_t final_size = 0;
    const char* output = writer.GetDataAndWriteSize(&final_size);
    EXPECT_EQ(byte_count * 2, final_size);

    // First 8 bytes should encode the size (little-endian).
    EXPECT_EQ(byte_count * 2, output[0]);
    EXPECT_EQ(0, output[1]);
    EXPECT_EQ(0, output[2]);
    EXPECT_EQ(0, output[3]);
    EXPECT_EQ(0, output[4]);
    EXPECT_EQ(0, output[5]);
    EXPECT_EQ(0, output[6]);
    EXPECT_EQ(0, output[7]);

    // Remaining bytes should be identical to their indices mod the array size (since it was
    // written twice).
    for (uint64_t i = 8; i < final_size; i++) {
        EXPECT_EQ(i % byte_count, output[i]);
    }

    MessageReader reader(output, final_size);
    uint64_t read_size = 0;
    ASSERT_TRUE(reader.ReadUint64(&read_size));
    EXPECT_EQ(byte_count * 2, read_size);

    char read_first[byte_count - 8];
    ASSERT_TRUE(reader.ReadBytes(byte_count - 8, read_first));
    EXPECT_EQ(8, read_first[0]);
    EXPECT_EQ(9, read_first[1]);
    EXPECT_EQ(10, read_first[2]);
    EXPECT_EQ(11, read_first[3]);

    char read_second[byte_count];
    ASSERT_TRUE(reader.ReadBytes(byte_count, read_second));
    for (uint64_t i = 0; i < byte_count; i++) {
        EXPECT_EQ(i, read_second[i]);
    }

    // Reading one more byte should fail.
    EXPECT_FALSE(reader.has_error());
    char one_more;
    EXPECT_FALSE(reader.ReadBytes(1, &one_more));
    EXPECT_TRUE(reader.has_error());
}

TEST(Message, ReadWriteNumbers) {
    MessageWriter writer;
    writer.WriteUint64(0);  // Message size header.

    int64_t expected_int64 = -7;
    uint64_t expected_uint64 = static_cast<uint64_t>(-8);

    writer.WriteInt64(expected_int64);
    writer.WriteUint64(expected_uint64);

    uint64_t message_size = 0;
    const char* message = writer.GetDataAndWriteSize(&message_size);
    constexpr uint64_t expected_message_size = 24;
    ASSERT_EQ(expected_message_size, message_size);

    MessageReader reader(message, message_size);

    // Message size header.
    uint64_t read_message_size = 0;
    ASSERT_TRUE(reader.ReadUint64(&read_message_size));
    EXPECT_EQ(expected_message_size, read_message_size);

    int64_t read_int64 = 0;
    ASSERT_TRUE(reader.ReadInt64(&read_int64));
    EXPECT_EQ(expected_int64, read_int64);

    uint64_t read_uint64 = 0;
    ASSERT_TRUE(reader.ReadUint64(&read_uint64));
    EXPECT_EQ(expected_uint64, read_uint64);

    // Reading one more byte should fail.
    EXPECT_FALSE(reader.has_error());
    int64_t one_more;
    EXPECT_FALSE(reader.ReadInt64(&one_more));
    EXPECT_TRUE(reader.has_error());
}

#endif
