// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/debug_ipc/stream_buffer.h"

#include "gtest/gtest.h"

TEST(StreamBuffer, Basic) {
  StreamBuffer buf;
  char output[16];

  // Test the empty case.
  EXPECT_TRUE(buf.IsAvailable(0));
  EXPECT_FALSE(buf.IsAvailable(1));
  EXPECT_EQ(0u, buf.Read(output, sizeof(output)));
  EXPECT_EQ(0u, buf.Peek(output, sizeof(output)));

  size_t first_block_size = 3;
  std::vector<char> first_block(first_block_size);
  first_block[0] = 'a';
  first_block[1] = 'b';
  first_block[2] = 'c';
  buf.AddData(std::move(first_block));

  EXPECT_TRUE(buf.IsAvailable(0));
  EXPECT_TRUE(buf.IsAvailable(1));
  EXPECT_TRUE(buf.IsAvailable(3));
  EXPECT_FALSE(buf.IsAvailable(4));

  size_t second_block_size = 3;
  std::vector<char> second_block(second_block_size);
  second_block[0] = 'd';
  second_block[1] = 'e';
  second_block[2] = 'f';
  buf.AddData(std::move(second_block));

  size_t third_block_size = 5;
  std::vector<char> third_block(third_block_size);
  third_block[0] = 'g';
  third_block[1] = 'h';
  third_block[2] = 'i';
  third_block[3] = 'j';
  third_block[4] = 'k';
  buf.AddData(std::move(third_block));

  // Try a peek, the next read should give the same data.
  EXPECT_EQ(2u, buf.Peek(output, 2u));
  EXPECT_EQ('a', output[0]);
  EXPECT_EQ('b', output[1]);

  // This read goes to a block boundary exactly.
  EXPECT_EQ(first_block_size, buf.Read(output, first_block_size));
  EXPECT_EQ('a', output[0]);
  EXPECT_EQ('b', output[1]);
  EXPECT_EQ('c', output[2]);

  // Now do a read across blocks.
  EXPECT_EQ(5u, buf.Read(output, 5u));
  EXPECT_EQ('d', output[0]);
  EXPECT_EQ('e', output[1]);
  EXPECT_EQ('f', output[2]);
  EXPECT_EQ('g', output[3]);
  EXPECT_EQ('h', output[4]);

  // Now do a read off the end which should be partial.
  EXPECT_EQ(3u, buf.Read(output, 5u));
  EXPECT_EQ('i', output[0]);
  EXPECT_EQ('j', output[1]);
  EXPECT_EQ('k', output[2]);

  EXPECT_FALSE(buf.IsAvailable(1));
}
