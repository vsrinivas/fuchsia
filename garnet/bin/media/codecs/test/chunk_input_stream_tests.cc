// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/mediacodec/cpp/fidl.h>
#include <src/lib/fxl/logging.h>

#include <algorithm>

#include "../chunk_input_stream.h"
#include "gtest/gtest.h"

constexpr uint32_t kBufferLifetimeOrdinal = 1;

class CodecPacketForTest : public CodecPacket {
 public:
  CodecPacketForTest(uint32_t index)
      : CodecPacket(kBufferLifetimeOrdinal, index) {}
};

fuchsia::media::StreamBuffer StreamBufferOfSize(size_t size, uint32_t index) {
  zx::vmo vmo_handle;
  zx_status_t err = zx::vmo::create(size, 0u, &vmo_handle);
  FXL_CHECK(err == ZX_OK) << "Failed to create vmo: " << err;

  fuchsia::media::StreamBufferDataVmo vmo;
  vmo.set_vmo_handle(std::move(vmo_handle));
  vmo.set_vmo_usable_start(0);
  vmo.set_vmo_usable_size(size);

  fuchsia::media::StreamBufferData data;
  data.set_vmo(std::move(vmo));

  fuchsia::media::StreamBuffer buffer;
  buffer.set_data(std::move(data));
  buffer.set_buffer_index(index);
  buffer.set_buffer_lifetime_ordinal(kBufferLifetimeOrdinal);

  return buffer;
}

class CodecBufferForTest : public CodecBuffer {
 public:
  CodecBufferForTest(size_t size, uint32_t index)
      : CodecBuffer(/*parent=*/nullptr, kOutputPort,
                    StreamBufferOfSize(size, index)) {
    Init();
  }
};

struct TestPackets {
  std::vector<std::unique_ptr<CodecPacketForTest>> packets;
  std::vector<CodecPacket*> ptrs;
};

TestPackets Packets(size_t count) {
  TestPackets packets;
  for (size_t i = 0; i < count; ++i) {
    packets.packets.push_back(std::make_unique<CodecPacketForTest>(i));
    packets.ptrs.push_back(packets.packets[i].get());
  }
  return packets;
}

struct TestBuffers {
  std::vector<std::unique_ptr<CodecBufferForTest>> buffers;
  std::vector<const CodecBuffer*> ptrs;
};

TestBuffers Buffers(std::vector<size_t> sizes) {
  TestBuffers buffers;
  for (size_t i = 0; i < sizes.size(); ++i) {
    buffers.buffers.push_back(
        std::make_unique<CodecBufferForTest>(sizes[i], i));
    buffers.ptrs.push_back(buffers.buffers[i].get());
  }
  return buffers;
}

size_t AlignUp(size_t v, size_t alignment) {
  ZX_ASSERT(alignment);
  return (v + alignment - 1) / alignment * alignment;
}

TEST(ChunkInputStream, ChunkBoundaries) {
  srand(100);

  // Each test run creates a buffer that counts from 0 to (>=99), and packets
  // that point to contiguous regions in that buffer of random lengths. They
  // are fed to the chunk input stream and we expect to find the same sequence
  // of 0 to (>=99).
  auto test_chunk_size = [](size_t chunk_size) {
    // Ensures we send enough packets to get 100 bytes out. We may add more
    // bytes to complete a chunk and force the output.
    const size_t buffer_size = AlignUp(100, chunk_size);
    auto buffers = Buffers({buffer_size});
    auto buffer = buffers.ptrs[0];

    // Initialize buffer with bytes counting from 0 to (>=99).
    const uint8_t* end = buffer->buffer_base() + buffer->buffer_size();
    for (uint8_t* pos = buffer->buffer_base(); pos < end; ++pos) {
      *pos = pos - buffer->buffer_base();
    }

    // Assign packets random lengths until the buffer is accounted for.
    std::vector<std::pair<size_t, size_t>> packet_lengths_and_offsets;
    uint8_t* pos = buffer->buffer_base();
    while (pos < end) {
      size_t packet_length =
          std::min((rand() % 10) + 1, static_cast<int>(end - pos));
      packet_lengths_and_offsets.push_back(
          {packet_length, pos - buffer->buffer_base()});
      pos += packet_length;
    }

    auto packets = Packets(packet_lengths_and_offsets.size());
    size_t i = 0;
    for (auto [packet_length, packet_offset] : packet_lengths_and_offsets) {
      packets.ptrs[i]->SetValidLengthBytes(packet_length);
      packets.ptrs[i]->SetBuffer(buffer);
      packets.ptrs[i]->SetStartOffset(packet_offset);
      ++i;
    }

    size_t seen = 0;
    auto input_block_processor =
        [&seen, chunk_size](ChunkInputStream::InputBlock input_block) {
          EXPECT_EQ(input_block.len, chunk_size);
          EXPECT_EQ(input_block.non_padding_len, input_block.len);
          EXPECT_FALSE(input_block.is_end_of_stream);
          for (size_t i = 0; i < input_block.len; ++i) {
            EXPECT_EQ(*(input_block.data + i), static_cast<uint8_t>(seen));
            ++seen;
          }
          return ChunkInputStream::kContinue;
        };
    auto under_test = ChunkInputStream(chunk_size, TimestampExtrapolator(),
                                       std::move(input_block_processor));
    for (auto packet : packets.ptrs) {
      EXPECT_EQ(under_test.ProcessInputPacket(packet), ChunkInputStream::kOk);
    }
    EXPECT_EQ(seen, buffer_size) << "Failure on chunk size " << chunk_size;
  };

  for (size_t i = 0; i < 30u; ++i) {
    test_chunk_size((rand() % 50) + 1);
  }
}

TEST(ChunkInputStream, FlushIncomplete) {
  constexpr size_t kChunkSize = 5;
  constexpr size_t kPacketLen = 1;
  auto packets = Packets(1);
  auto buffers = Buffers({kPacketLen});

  auto packet = packets.ptrs[0];
  auto buffer = buffers.ptrs[0];

  constexpr size_t kExpectedByte = 44;
  packet->SetValidLengthBytes(kPacketLen);
  packet->SetBuffer(buffer);
  packet->SetStartOffset(0);
  *(buffer->buffer_base()) = kExpectedByte;

  bool was_called_for_input_block = false;
  bool flush_called = false;
  auto input_block_processor =
      [&was_called_for_input_block, &flush_called,
       kChunkSize](ChunkInputStream::InputBlock input_block) {
        if (input_block.is_end_of_stream) {
          flush_called = true;
          const size_t expected[kChunkSize] = {kExpectedByte};
          EXPECT_EQ(input_block.len, kChunkSize);
          EXPECT_EQ(input_block.non_padding_len, kPacketLen % kChunkSize);
          EXPECT_TRUE(input_block.is_end_of_stream);
          EXPECT_EQ(memcmp(input_block.data, expected, input_block.len), 0);
          return ChunkInputStream::kContinue;
        }

        was_called_for_input_block = true;
        return ChunkInputStream::kContinue;
      };

  auto under_test = ChunkInputStream(kChunkSize, TimestampExtrapolator(),
                                     input_block_processor);

  // We load the stream with one packet that is too short to complete a block,
  // and expect no input blocks to come from it.
  EXPECT_EQ(under_test.ProcessInputPacket(packet), ChunkInputStream::kOk);
  EXPECT_FALSE(was_called_for_input_block);

  // Now we flush and expect to get our data at the start of a buffer, with 0s
  // padded to complete a block.
  EXPECT_EQ(ChunkInputStream::kOk, under_test.Flush());
  EXPECT_TRUE(flush_called);
}

TEST(ChunkInputStream, FlushLeftover) {
  constexpr size_t kChunkSize = 5;
  constexpr size_t kPacketLen = 7;
  auto packets = Packets(1);
  auto buffers = Buffers({kPacketLen});

  auto packet = packets.ptrs[0];
  auto buffer = buffers.ptrs[0];

  const uint8_t kExpectedBytes[kPacketLen] = {3, 4, 5, 88, 92, 101, 77};
  packet->SetValidLengthBytes(kPacketLen);
  packet->SetBuffer(buffer);
  packet->SetStartOffset(0);
  memcpy(buffer->buffer_base(), kExpectedBytes, kPacketLen);

  size_t input_block_call_count = 0;
  bool flush_called = false;
  auto input_block_processor =
      [&input_block_call_count, &flush_called, kChunkSize,
       kExpectedBytes](ChunkInputStream::InputBlock input_block) {
        if (input_block.is_end_of_stream) {
          flush_called = true;
          const uint8_t expected[kChunkSize] = {kExpectedBytes[kPacketLen - 2],
                                                kExpectedBytes[kPacketLen - 1]};
          EXPECT_EQ(input_block.len, kChunkSize);
          EXPECT_EQ(input_block.non_padding_len, kPacketLen % kChunkSize);
          EXPECT_TRUE(input_block.is_end_of_stream);
          EXPECT_EQ(memcmp(input_block.data, expected, input_block.len), 0);
          return ChunkInputStream::kContinue;
        }

        input_block_call_count += 1;
        EXPECT_NE(input_block.data, nullptr);
        EXPECT_EQ(input_block.len, kChunkSize);
        EXPECT_EQ(input_block.non_padding_len, input_block.len);
        EXPECT_FALSE(input_block.is_end_of_stream);
        EXPECT_EQ(memcmp(input_block.data, kExpectedBytes, kChunkSize), 0);
        return ChunkInputStream::kContinue;
      };

  auto under_test = ChunkInputStream(kChunkSize, TimestampExtrapolator(),
                                     input_block_processor);

  // We send a packet that is long enough for an input block and a little of the
  // next input block. We expect only one complete input block.
  EXPECT_EQ(under_test.ProcessInputPacket(packet), ChunkInputStream::kOk);
  EXPECT_EQ(input_block_call_count, 1u);

  // Now we flush and expect the leftover data in a buffer with padded 0s to
  // complete the input block.
  EXPECT_EQ(under_test.Flush(), ChunkInputStream::kOk);
  EXPECT_TRUE(flush_called);
}

TEST(ChunkInputStream, TimestampsCarry) {
  constexpr size_t kChunkSize = 5;
  constexpr size_t kPacketLen = 7;
  auto packets = Packets(1);
  auto buffers = Buffers({kPacketLen});

  auto packet = packets.ptrs[0];
  auto buffer = buffers.ptrs[0];

  const uint64_t kExpectedTimestamp = 30;
  packet->SetValidLengthBytes(kPacketLen);
  packet->SetBuffer(buffer);
  packet->SetStartOffset(0);
  packet->SetTimstampIsh(kExpectedTimestamp);

  bool was_called_for_input_block = false;
  bool flush_called = false;
  auto input_block_processor =
      [&was_called_for_input_block, &flush_called,
       kExpectedTimestamp](ChunkInputStream::InputBlock input_block) {
        if (input_block.is_end_of_stream) {
          flush_called = true;
          EXPECT_EQ(input_block.timestamp_ish.has_value(), false);
          return ChunkInputStream::kContinue;
        }

        was_called_for_input_block = true;
        EXPECT_EQ(input_block.timestamp_ish.has_value(), true);
        EXPECT_EQ(input_block.timestamp_ish.value_or(0), kExpectedTimestamp);
        return ChunkInputStream::kContinue;
      };

  auto under_test = ChunkInputStream(kChunkSize, TimestampExtrapolator(),
                                     input_block_processor);

  // We expect our single timestamp to come in the first input block.
  EXPECT_EQ(under_test.ProcessInputPacket(packet), ChunkInputStream::kOk);
  EXPECT_TRUE(was_called_for_input_block);

  // We expect that the timestamp was consumed.
  EXPECT_EQ(under_test.Flush(), ChunkInputStream::kOk);
  EXPECT_TRUE(flush_called);
}

TEST(ChunkInputStream, TimestampsExtrapolate) {
  constexpr size_t kChunkSize = 5;
  constexpr size_t kPacketLen = 4;
  auto our_extrapolator = TimestampExtrapolator(ZX_SEC(1), ZX_SEC(1));
  auto stream_extrapolator = our_extrapolator;
  auto packets = Packets(2);
  auto buffers = Buffers({kPacketLen, kPacketLen});

  // Configure two packets, the first length 4. The second will contain a
  // timestamp. Since the chunk size is 5, the second packet will need its
  // timestamp extrapolated 1 byte.
  packets.ptrs[0]->SetValidLengthBytes(kPacketLen);
  packets.ptrs[0]->SetStartOffset(0);
  packets.ptrs[0]->SetBuffer(buffers.ptrs[0]);
  const uint64_t kInputTimestamp = 30;
  our_extrapolator.Inform(4, kInputTimestamp);
  const uint64_t kExpectedTimestamp = *our_extrapolator.Extrapolate(5);
  packets.ptrs[1]->SetValidLengthBytes(kPacketLen);
  packets.ptrs[1]->SetBuffer(buffers.ptrs[1]);
  packets.ptrs[1]->SetStartOffset(0);
  packets.ptrs[1]->SetTimstampIsh(kInputTimestamp);

  size_t packet_index = 0;  // We use this to run different code when processing
                            // each packet.
  bool was_called_for_packet_0 = false;
  bool was_called_for_packet_1 = false;
  bool flush_called = false;

  auto input_block_processor =
      [&packet_index, &was_called_for_packet_0, &was_called_for_packet_1,
       &flush_called,
       kExpectedTimestamp](ChunkInputStream::InputBlock input_block) {
        if (input_block.is_end_of_stream) {
          flush_called = true;
          EXPECT_EQ(input_block.timestamp_ish.has_value(), true);
          EXPECT_EQ(input_block.timestamp_ish.value_or(0), kExpectedTimestamp);
          return ChunkInputStream::kContinue;
        }

        switch (packet_index) {
          case 0:
            was_called_for_packet_0 = true;
            return ChunkInputStream::kContinue;
          case 1:
            was_called_for_packet_1 = true;
            EXPECT_EQ(input_block.timestamp_ish.has_value(), false);
            return ChunkInputStream::kContinue;
          default:
            EXPECT_FALSE(true) << "This should not happen.";
            return ChunkInputStream::kTerminate;
        }
      };

  auto under_test = ChunkInputStream(kChunkSize, std::move(stream_extrapolator),
                                     input_block_processor);

  // We send a short packet in that isn't a full input block to bring our
  // stream out of alignment. This one doesn't have a timestamp.
  EXPECT_EQ(under_test.ProcessInputPacket(packets.ptrs[0]),
            ChunkInputStream::kOk);
  EXPECT_FALSE(was_called_for_packet_0);

  // We send in a packet to complete the first block. It should not have a
  // timestamp even though the new packet has one, because we only extrapolate
  // forward.
  packet_index += 1;
  EXPECT_EQ(under_test.ProcessInputPacket(packets.ptrs[1]),
            ChunkInputStream::kOk);
  EXPECT_TRUE(was_called_for_packet_1);

  // We expect the flush to contain a timestamp extrapolated from the second
  // packet's timestamp.
  EXPECT_EQ(under_test.Flush(), ChunkInputStream::kOk);
  EXPECT_TRUE(flush_called);
}

TEST(ChunkInputStream, TimestampsDropWhenInsideBlock) {
  constexpr size_t kChunkSize = 5;
  constexpr size_t kPacketLen = 1;
  auto packets = Packets(4);
  auto buffers = Buffers({kPacketLen, kPacketLen, kPacketLen, kChunkSize});

  // Configure 4 packets, each with a timestamp, all starting in the same
  // input block because they are small. In the output we should see the
  // timestamp for the first packet, and a timestamp extrapolated from the 4th
  // packet, where the middle 2 timestamps do not influence the output.
  const uint64_t kExpectedTimestamp = 5;
  packets.ptrs[0]->SetValidLengthBytes(kPacketLen);
  packets.ptrs[0]->SetStartOffset(0);
  packets.ptrs[0]->SetBuffer(buffers.ptrs[0]);
  packets.ptrs[0]->SetTimstampIsh(kExpectedTimestamp);
  packets.ptrs[1]->SetValidLengthBytes(kPacketLen);
  packets.ptrs[1]->SetBuffer(buffers.ptrs[1]);
  packets.ptrs[1]->SetStartOffset(0);
  packets.ptrs[1]->SetTimstampIsh(4096);
  packets.ptrs[2]->SetValidLengthBytes(kPacketLen);
  packets.ptrs[2]->SetBuffer(buffers.ptrs[2]);
  packets.ptrs[2]->SetStartOffset(0);
  packets.ptrs[2]->SetTimstampIsh(2048);

  packets.ptrs[3]->SetValidLengthBytes(kChunkSize);
  packets.ptrs[3]->SetBuffer(buffers.ptrs[3]);
  packets.ptrs[3]->SetStartOffset(0);
  packets.ptrs[3]->SetTimstampIsh(10);
  const uint64_t kExpectedExtrapolatedTimestamp = 12;

  size_t packet_index = 0;  // We use this to run different code when processing
                            // each packet.
  bool was_called_for_packet_0 = false;
  bool was_called_for_packet_1 = false;
  bool was_called_for_packet_2 = false;
  bool was_called_for_packet_3 = false;
  bool flush_called = false;

  auto input_block_processor = [&packet_index, &was_called_for_packet_0,
                                &was_called_for_packet_1,
                                &was_called_for_packet_2,
                                &was_called_for_packet_3, kExpectedTimestamp,
                                &flush_called, kExpectedExtrapolatedTimestamp](
                                   ChunkInputStream::InputBlock input_block) {
    if (input_block.is_end_of_stream) {
      flush_called = true;
      EXPECT_EQ(input_block.timestamp_ish.has_value(), true);
      EXPECT_EQ(input_block.timestamp_ish.value_or(0),
                kExpectedExtrapolatedTimestamp);
      return ChunkInputStream::kContinue;
    }

    switch (packet_index) {
      case 0:
        was_called_for_packet_0 = true;
        return ChunkInputStream::kContinue;
      case 1:
        was_called_for_packet_1 = true;
        return ChunkInputStream::kContinue;
      case 2:
        was_called_for_packet_2 = true;
        return ChunkInputStream::kContinue;
      case 3:
        was_called_for_packet_3 = true;
        EXPECT_EQ(input_block.timestamp_ish.has_value(), true);
        EXPECT_EQ(input_block.timestamp_ish.value_or(0), kExpectedTimestamp);
        return ChunkInputStream::kContinue;
      default:
        EXPECT_FALSE(true) << "This should not happen.";
        return ChunkInputStream::kTerminate;
    }
  };

  auto under_test =
      ChunkInputStream(kChunkSize, TimestampExtrapolator(ZX_SEC(1), ZX_SEC(1)),
                       input_block_processor);

  EXPECT_EQ(under_test.ProcessInputPacket(packets.ptrs[0]),
            ChunkInputStream::kOk);
  EXPECT_FALSE(was_called_for_packet_0);

  packet_index += 1;
  EXPECT_EQ(under_test.ProcessInputPacket(packets.ptrs[1]),
            ChunkInputStream::kOk);
  EXPECT_FALSE(was_called_for_packet_1);

  packet_index += 1;
  EXPECT_EQ(under_test.ProcessInputPacket(packets.ptrs[2]),
            ChunkInputStream::kOk);
  EXPECT_FALSE(was_called_for_packet_2);

  packet_index += 1;
  EXPECT_EQ(under_test.ProcessInputPacket(packets.ptrs[3]),
            ChunkInputStream::kOk);
  EXPECT_TRUE(was_called_for_packet_3);

  EXPECT_EQ(ChunkInputStream::kOk, under_test.Flush());
  EXPECT_TRUE(flush_called);
}

TEST(ChunkInputStream, ReportsErrorWhenMissingTimebase) {
  constexpr size_t kChunkSize = 5;
  auto packets = Packets(2);
  auto buffers = Buffers({4, 20});

  // Configure two packets, the first length 4. The second will contain a
  // timestamp. Since the chunk size is 5, the second packet will need its
  // timestamp extrapolated 1 byte.
  packets.ptrs[0]->SetValidLengthBytes(buffers.ptrs[0]->buffer_size());
  packets.ptrs[0]->SetStartOffset(0);
  packets.ptrs[0]->SetBuffer(buffers.ptrs[0]);

  const uint64_t kInputTimestamp = 30;
  packets.ptrs[1]->SetValidLengthBytes(buffers.ptrs[1]->buffer_size());
  packets.ptrs[1]->SetBuffer(buffers.ptrs[1]);
  packets.ptrs[1]->SetStartOffset(0);
  packets.ptrs[1]->SetTimstampIsh(kInputTimestamp);

  size_t packet_index = 0;  // We use this to run different code when processing
                            // each packet.
  bool was_called_for_packet_0 = false;
  size_t calls_for_packet_2 = 0;
  auto input_block_processor =
      [&packet_index, &was_called_for_packet_0,
       &calls_for_packet_2](ChunkInputStream::InputBlock input_block) {
        switch (packet_index) {
          case 0:
            was_called_for_packet_0 = true;
            return ChunkInputStream::kContinue;
          case 1:
            calls_for_packet_2 += 1;
            return ChunkInputStream::kContinue;
          default:
            EXPECT_TRUE(false) << "This should not happen.";
            return ChunkInputStream::kTerminate;
        }
      };

  auto under_test = ChunkInputStream(kChunkSize, TimestampExtrapolator(),
                                     input_block_processor);

  EXPECT_EQ(under_test.ProcessInputPacket(packets.ptrs[0]),
            ChunkInputStream::kOk);
  EXPECT_FALSE(was_called_for_packet_0);

  packet_index += 1;
  EXPECT_EQ(under_test.ProcessInputPacket(packets.ptrs[1]),
            ChunkInputStream::kExtrapolationFailedWithoutTimebase);
  // Should have been called once for finishing the first input packet, without
  // timestamp.
  EXPECT_EQ(calls_for_packet_2, 1u);
}
