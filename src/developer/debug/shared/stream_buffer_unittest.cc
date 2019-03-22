// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/shared/stream_buffer.h"

#include "gtest/gtest.h"

namespace debug_ipc {

namespace {

// Implements a simple sink
class Writer : public debug_ipc::StreamBuffer::Writer {
 public:
  // This class reads only up to a given amount of data. It starts as 0 (it
  // can't read anything) and can be increased with this function.
  void set_read_amount(size_t amount) { read_amount_ = amount; }

  // StreamBuffer::Writer implementation.
  size_t ConsumeStreamBufferData(const char* data, size_t len) override {
    size_t to_read = std::min(read_amount_, len);
    data_.insert(data_.end(), data, &data[to_read]);
    read_amount_ -= to_read;
    return to_read;
  }

  const std::vector<char>& data() { return data_; }

 private:
  std::vector<char> data_;
  size_t read_amount_ = 0;
};

}  // namespace

TEST(StreamBuffer, Read) {
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
  buf.AddReadData(std::move(first_block));

  EXPECT_TRUE(buf.IsAvailable(0));
  EXPECT_TRUE(buf.IsAvailable(1));
  EXPECT_TRUE(buf.IsAvailable(3));
  EXPECT_FALSE(buf.IsAvailable(4));

  size_t second_block_size = 3;
  std::vector<char> second_block(second_block_size);
  second_block[0] = 'd';
  second_block[1] = 'e';
  second_block[2] = 'f';
  buf.AddReadData(std::move(second_block));

  size_t third_block_size = 5;
  std::vector<char> third_block(third_block_size);
  third_block[0] = 'g';
  third_block[1] = 'h';
  third_block[2] = 'i';
  third_block[3] = 'j';
  third_block[4] = 'k';
  buf.AddReadData(std::move(third_block));

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

TEST(StreamBuffer, Write) {
  Writer sink;
  StreamBuffer buf;
  buf.set_writer(&sink);

  // Write when the writer isn't ready.
  std::vector<char> block_one;
  block_one.push_back(0);
  block_one.push_back(1);
  block_one.push_back(2);
  buf.Write(std::move(block_one));

  EXPECT_TRUE(sink.data().empty());

  // Read two of the bytes available.
  sink.set_read_amount(2);
  buf.SetWritable();
  EXPECT_EQ(2u, sink.data().size());

  // Add two more blocks of pending writes.
  std::vector<char> block_two;
  block_two.push_back(3);
  block_two.push_back(4);
  block_two.push_back(5);
  buf.Write(std::move(block_two));

  std::vector<char> block_three;
  block_three.push_back(6);
  block_three.push_back(7);
  block_three.push_back(8);
  block_three.push_back(9);
  block_three.push_back(10);
  buf.Write(std::move(block_three));

  // Read to the middle of the last block (this will consume two), then
  // consume the rest.
  sink.set_read_amount(6);
  buf.SetWritable();
  sink.set_read_amount(1000);
  buf.SetWritable();

  ASSERT_EQ(11u, sink.data().size());
  for (size_t i = 0; i < sink.data().size(); i++)
    EXPECT_EQ(i, static_cast<size_t>(sink.data()[i]));
}

}  // namespace debug_ipc
