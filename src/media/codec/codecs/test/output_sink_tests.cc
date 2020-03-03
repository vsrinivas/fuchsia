// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/mediacodec/cpp/fidl.h>

#include <algorithm>
#include <map>

#include "../output_sink.h"
#include "gtest/gtest.h"
#include "test_codec_packets.h"

TEST(OutputSink, Basic) {
  auto test_with_buffers = [](TestBuffers&& buffers) {
    size_t total_read = 0;
    fit::function<void(CodecPacket*)> recycle;
    auto sender = [&total_read, &recycle](CodecPacket* output_packet) {
      const uint8_t* input = output_packet->buffer()->base() + output_packet->start_offset();
      EXPECT_FALSE(output_packet->has_timestamp_ish());
      for (size_t i = 0; i < output_packet->valid_length_bytes(); ++i) {
        EXPECT_EQ(input[i], (total_read + i) % 256);
      }
      total_read += output_packet->valid_length_bytes();
      recycle(output_packet);
      return OutputSink::kSuccess;
    };

    auto under_test = OutputSink(sender, thrd_current());
    recycle = [&under_test](CodecPacket* packet) { under_test.AddOutputPacket(packet); };

    size_t total_bytes = 0;
    for (size_t i = 0; i < buffers.buffers.size(); ++i) {
      auto buffer = buffers.ptr(i);
      under_test.AddOutputBuffer(buffer);
      total_bytes += buffer->size();
    }

    size_t smallest_buffer_size = INT_MAX;
    for (size_t i = 0; i < buffers.buffers.size(); ++i) {
      auto buffer = buffers.ptr(i);
      if (buffer->size() < smallest_buffer_size) {
        smallest_buffer_size = buffer->size();
      }
    }

    total_bytes /= 2;
    std::vector<size_t> write_sizes;
    while (total_bytes > 0) {
      const size_t write_size =
          std::min(total_bytes, std::max(rand() % smallest_buffer_size, static_cast<size_t>(1)));
      write_sizes.push_back(write_size);
      total_bytes -= write_size;
    }

    // We allocate enough packets for each write size to be a different packet.
    // The relationship doesn't need to be 1:1, but we know it will never be
    // valid to require more than N packets to emit N writes.
    auto packets = Packets(write_sizes.size());
    for (size_t i = 0; i < packets.packets.size(); ++i) {
      under_test.AddOutputPacket(packets.ptr(i));
    }

    size_t total_written = 0;
    for (auto write_size : write_sizes) {
      auto status = under_test.NextOutputBlock(
          write_size, /*timestamp=*/std::nullopt,
          [write_size, &total_written](OutputSink::OutputBlock output_block) mutable
          -> std::pair<size_t, OutputSink::UserStatus> {
            EXPECT_NE(output_block.data, nullptr);
            EXPECT_NE(output_block.buffer, nullptr);
            EXPECT_EQ(output_block.len, write_size);
            for (size_t i = 0; i < write_size; ++i) {
              output_block.data[i] = (total_written + i) % 256;
            }
            total_written += write_size;
            return {write_size, OutputSink::kSuccess};
          });
      EXPECT_EQ(status, OutputSink::kOk);
    }

    EXPECT_EQ(under_test.Flush(), OutputSink::kOk);
    EXPECT_EQ(total_read, total_written);
  };

  test_with_buffers(Buffers({30, 400, 200, 12, 11, 13}));
  test_with_buffers(Buffers({23, 29, 31, 37, 43, 47}));
  test_with_buffers(Buffers({241, 547, 809, 16, 256, 283}));
  test_with_buffers(Buffers({128, 256, 512, 1024, 1023, 997}));
}

TEST(OutputSink, ReportsSendError) {
  bool send_called = false;
  auto sender = [&send_called](CodecPacket* output_packet) {
    send_called = true;
    return OutputSink::kError;
  };

  auto under_test = OutputSink(sender, thrd_current());
  auto buffers = Buffers({100});
  under_test.AddOutputBuffer(buffers.ptr(0));
  auto packets = Packets(1);
  under_test.AddOutputPacket(packets.ptr(0));

  auto status = under_test.NextOutputBlock(
      10, /*timestamp=*/
      std::nullopt,
      [](OutputSink::OutputBlock output_block) -> std::pair<size_t, OutputSink::UserStatus> {
        return {10u, OutputSink::kSuccess};
      });
  EXPECT_EQ(status, OutputSink::kOk);
  EXPECT_EQ(under_test.Flush(), OutputSink::kUserError);
  EXPECT_TRUE(send_called);
}

TEST(OutputSink, ReportsBuffersTooSmallError) {
  auto sender = [](CodecPacket* output_packet) { return OutputSink::kError; };

  auto under_test = OutputSink(sender, thrd_current());
  auto buffers = Buffers({1});
  under_test.AddOutputBuffer(buffers.ptr(0));
  auto packets = Packets(1);
  under_test.AddOutputPacket(packets.ptr(0));

  auto status = under_test.NextOutputBlock(
      10, /*timestamp=*/
      std::nullopt,
      [](OutputSink::OutputBlock output_block) -> std::pair<size_t, OutputSink::UserStatus> {
        return {10u, OutputSink::kSuccess};
      });
  EXPECT_EQ(status, OutputSink::kBuffersTooSmall);
}

TEST(OutputSink, ReportsBuffersTooSmallAtRequestTime) {
  // It's important we reject small buffers at request time, because we may have
  // only have one buffer.

  auto sender = [](CodecPacket* output_packet) { return OutputSink::kSuccess; };

  auto under_test = OutputSink(sender, thrd_current());
  auto buffers = Buffers({2, 1});
  under_test.AddOutputBuffer(buffers.ptr(0));
  under_test.AddOutputBuffer(buffers.ptr(1));
  auto packets = Packets(2);
  under_test.AddOutputPacket(packets.ptr(0));
  under_test.AddOutputPacket(packets.ptr(1));

  {
    auto status = under_test.NextOutputBlock(
        2, /*timestamp=*/
        std::nullopt,
        [](OutputSink::OutputBlock output_block) -> std::pair<size_t, OutputSink::UserStatus> {
          return {2u, OutputSink::kSuccess};
        });
    EXPECT_EQ(status, OutputSink::kOk);
  }

  {
    auto status = under_test.NextOutputBlock(
        2, /*timestamp=*/
        std::nullopt,
        [](OutputSink::OutputBlock output_block) -> std::pair<size_t, OutputSink::UserStatus> {
          return {2u, OutputSink::kSuccess};
        });
    EXPECT_EQ(status, OutputSink::kBuffersTooSmall);
  }
}

TEST(OutputSink, StopsAllWaits) {
  auto sender = [](CodecPacket* output_packet) { return OutputSink::kSuccess; };

  auto under_test = OutputSink(sender, thrd_current());
  under_test.StopAllWaits();
  auto status = under_test.NextOutputBlock(
      1, /*timestamp=*/
      std::nullopt,
      [](OutputSink::OutputBlock output_block) -> std::pair<size_t, OutputSink::UserStatus> {
        return {1u, OutputSink::kSuccess};
      });
  EXPECT_EQ(status, OutputSink::kUserTerminatedWait);
}

TEST(OutputSink, TimestampsPropagate) {
  constexpr uint64_t kExpectedTimestamp = 334;
  bool send_called = false;
  auto sender = [&send_called, kExpectedTimestamp](CodecPacket* output_packet) {
    send_called = true;
    EXPECT_TRUE(output_packet->has_timestamp_ish());
    EXPECT_EQ(output_packet->timestamp_ish(), kExpectedTimestamp);
    return OutputSink::kSuccess;
  };

  auto under_test = OutputSink(sender, thrd_current());
  auto buffers = Buffers({100});
  under_test.AddOutputBuffer(buffers.ptr(0));
  auto packets = Packets(1);
  under_test.AddOutputPacket(packets.ptr(0));

  auto status = under_test.NextOutputBlock(
      1, /*timestamp=*/
      {kExpectedTimestamp},
      [](OutputSink::OutputBlock output_block) -> std::pair<size_t, OutputSink::UserStatus> {
        return {1u, OutputSink::kSuccess};
      });
  EXPECT_EQ(status, OutputSink::kOk);

  EXPECT_EQ(under_test.Flush(), OutputSink::kOk);
  EXPECT_TRUE(send_called);
}

TEST(OutputSink, BlocksResize) {
  std::optional<int> emitted_packet_size;
  auto sender = [&emitted_packet_size](CodecPacket* output_packet) {
    emitted_packet_size = output_packet->valid_length_bytes();
    return OutputSink::kSuccess;
  };

  auto under_test = OutputSink(sender, thrd_current());
  auto buffers = Buffers({100});
  under_test.AddOutputBuffer(buffers.ptr(0));
  auto packets = Packets(1);
  under_test.AddOutputPacket(packets.ptr(0));

  auto status = under_test.NextOutputBlock(
      100, std::nullopt,
      [](OutputSink::OutputBlock output_block) -> std::pair<size_t, OutputSink::UserStatus> {
        return {50u, OutputSink::kSuccess};
      });
  EXPECT_EQ(status, OutputSink::kOk);

  EXPECT_EQ(under_test.Flush(), OutputSink::kOk);
  EXPECT_EQ(emitted_packet_size.value_or(-1), 50);
}

TEST(OutputSink, RespectsWriteError) {
  bool send_called = false;
  auto sender = [&send_called](CodecPacket* output_packet) {
    send_called = true;
    return OutputSink::kSuccess;
  };

  auto under_test = OutputSink(sender, thrd_current());
  auto buffers = Buffers({100});
  under_test.AddOutputBuffer(buffers.ptr(0));
  auto packets = Packets(1);
  under_test.AddOutputPacket(packets.ptr(0));

  auto status = under_test.NextOutputBlock(
      100, std::nullopt,
      [](OutputSink::OutputBlock output_block) -> std::pair<size_t, OutputSink::UserStatus> {
        return {0, OutputSink::kError};
      });
  EXPECT_EQ(status, OutputSink::kUserError);

  EXPECT_EQ(under_test.Flush(), OutputSink::kOk);
  EXPECT_FALSE(send_called);
}
