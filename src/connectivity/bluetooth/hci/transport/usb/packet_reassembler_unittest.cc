// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "packet_reassembler.h"

#include <vector>

#include <gtest/gtest.h>

namespace {

using TestReassembler = bt_transport_usb::PacketReassembler<10>;
constexpr size_t kLengthParamIndex = 2;
constexpr size_t kHeaderSize = 3;

TEST(PacketReassemblerTest, ProcessMultipleCompletePackets) {
  std::vector<uint8_t> output;
  TestReassembler recomb(kLengthParamIndex, kHeaderSize, [&](cpp20::span<const uint8_t> p) {
    std::copy(p.begin(), p.end(), std::back_inserter(output));
  });
  const std::vector<uint8_t> buffer_0 = {
      0x00, 0x00,  // handle + status parameters
      0x02,        // length
      0x03, 0x04   // payload
  };
  recomb.ProcessData(buffer_0);
  EXPECT_EQ(output, buffer_0);
  output.clear();

  const std::vector<uint8_t> buffer_1 = {
      0x00, 0x00,  // handle + status parameters
      0x02,        // length
      0x05, 0x06   // payload
  };
  recomb.ProcessData(buffer_1);
  EXPECT_EQ(output, buffer_1);
}

TEST(PacketReassemblerTest, ProcessMultiplePacketsWithMultipleChunks) {
  std::vector<uint8_t> output;
  TestReassembler recomb(kLengthParamIndex, kHeaderSize, [&](cpp20::span<const uint8_t> p) {
    std::copy(p.begin(), p.end(), std::back_inserter(output));
  });

  const std::vector<uint8_t> buffer_0 = {
      0x00, 0x00,  // handle + status parameters
      0x02,        // length
      0x03, 0x04   // payload
  };
  size_t first_chunk_size = 3;
  recomb.ProcessData({buffer_0.data(), first_chunk_size});
  EXPECT_TRUE(output.empty());
  recomb.ProcessData({buffer_0.data() + first_chunk_size, buffer_0.size() - first_chunk_size});
  EXPECT_EQ(output, buffer_0);
  output.clear();

  const std::vector<uint8_t> buffer_1 = {
      0x00, 0x00,       // handle + status parameters
      0x03,             // length
      0x01, 0x02, 0x03  // payload
  };
  first_chunk_size = 4;
  recomb.ProcessData({buffer_1.data(), first_chunk_size});
  EXPECT_TRUE(output.empty());
  recomb.ProcessData({buffer_1.data() + first_chunk_size, buffer_1.size() - first_chunk_size});
  EXPECT_EQ(output, buffer_1);
}

TEST(PacketReassemblerTest, ProcessIncompleteHeader) {
  std::vector<uint8_t> output;
  TestReassembler recomb(kLengthParamIndex, kHeaderSize, [&](cpp20::span<const uint8_t> p) {
    std::copy(p.begin(), p.end(), std::back_inserter(output));
  });

  const std::vector<uint8_t> buffer_0 = {
      0x00, 0x00,  // handle + status parameters
      0x02,        // length
      0x03, 0x04   // payload
  };
  // Process only first 2 bytes of header
  const size_t first_chunk_size = 2;
  recomb.ProcessData({buffer_0.data(), first_chunk_size});
  EXPECT_TRUE(output.empty());
  // Process the rest of the packet.
  recomb.ProcessData({buffer_0.data() + first_chunk_size, buffer_0.size() - first_chunk_size});
  EXPECT_EQ(output, buffer_0);
}

TEST(PacketReassemblerTest, ProcessMultiplePacketsInOneBuffer) {
  std::vector<std::vector<uint8_t>> output;
  TestReassembler recomb(kLengthParamIndex, kHeaderSize, [&](cpp20::span<const uint8_t> p) {
    output.emplace_back(p.begin(), p.end());
  });

  const size_t packet_0_size = 5;
  const size_t packet_1_size = 6;
  const size_t packet_2_size = 4;
  const std::vector<uint8_t> buffer = {
      // Packet 0
      0x00,  // handle + status parameters
      0x00,
      0x02,  // length
      0x03,  // payload
      0x04,
      // Packet 1
      0x00,  // handle + status parameters
      0x00,
      0x03,  // length
      0x05,  // payload
      0x06, 0x07,
      // Packet 2
      0x00,  // handle + status parameters
      0x00,
      0x01,  // length
      0x08,  // payload
  };
  // First 2 packets and first byte of third packet.
  const size_t first_chunk_size = packet_0_size + packet_1_size + 1;
  recomb.ProcessData({buffer.data(), first_chunk_size});
  EXPECT_EQ(output.size(), 2u);
  EXPECT_EQ(output[0], std::vector(buffer.data(), buffer.data() + packet_0_size));
  EXPECT_EQ(output[1], std::vector(buffer.data() + packet_0_size,
                                   buffer.data() + packet_0_size + packet_1_size));
  recomb.ProcessData({buffer.data() + first_chunk_size, buffer.size() - first_chunk_size});
  EXPECT_EQ(output.size(), 3u);
  EXPECT_EQ(output[2], std::vector(buffer.data() + packet_0_size + packet_1_size,
                                   buffer.data() + packet_0_size + packet_1_size + packet_2_size));
}

TEST(PacketReassemblerTest, ProcessPacketSplitIntoManyOneByteBuffers) {
  std::vector<std::vector<uint8_t>> output;
  TestReassembler recomb(kLengthParamIndex, kHeaderSize, [&](cpp20::span<const uint8_t> p) {
    output.emplace_back(p.begin(), p.end());
  });

  const std::vector<uint8_t> buffer = {
      0x00,  // handle + status parameters
      0x00,
      0x02,  // length
      0x03,  // payload
      0x04,
  };
  recomb.ProcessData({buffer.data(), 1});
  EXPECT_EQ(output.size(), 0u);
  recomb.ProcessData({buffer.data() + 1, 1});
  EXPECT_EQ(output.size(), 0u);
  recomb.ProcessData({buffer.data() + 2, 1});
  EXPECT_EQ(output.size(), 0u);
  recomb.ProcessData({buffer.data() + 3, 1});
  EXPECT_EQ(output.size(), 0u);
  recomb.ProcessData({buffer.data() + 4, 1});
  EXPECT_EQ(output.size(), 1u);
  EXPECT_EQ(output[0], buffer);
}

TEST(PacketReassemblerTest, ProcessBufferContainingEndOfFirstPacketAndASecondPacket) {
  std::vector<std::vector<uint8_t>> output;
  TestReassembler recomb(kLengthParamIndex, kHeaderSize, [&](cpp20::span<const uint8_t> p) {
    output.emplace_back(p.begin(), p.end());
  });

  const size_t packet_0_size = 5;
  const size_t packet_1_size = 6;
  const std::vector<uint8_t> buffer = {
      // Packet 0
      0x00,  // handle + status parameters
      0x00,
      0x02,  // length
      0x03,  // payload
      0x04,
      // Packet 1
      0x00,  // handle + status parameters
      0x00,
      0x03,  // length
      0x05,  // payload
      0x06,
      0x07,
  };
  const size_t first_chunk_size = packet_0_size - 1;
  recomb.ProcessData({buffer.data(), first_chunk_size});
  EXPECT_EQ(output.size(), 0u);
  recomb.ProcessData({buffer.data() + first_chunk_size, buffer.size() - first_chunk_size});
  EXPECT_EQ(output.size(), 2u);
  EXPECT_EQ(output[0], std::vector(buffer.data(), buffer.data() + packet_0_size));
  EXPECT_EQ(output[1], std::vector(buffer.data() + packet_0_size,
                                   buffer.data() + packet_0_size + packet_1_size));
}

TEST(PacketReassemblerTest, ProcessBufferContainingEndOfFirstPacketAndBeginningOfSecondPacket) {
  std::vector<std::vector<uint8_t>> output;
  TestReassembler recomb(kLengthParamIndex, kHeaderSize, [&](cpp20::span<const uint8_t> p) {
    output.emplace_back(p.begin(), p.end());
  });

  const size_t packet_0_size = 5;
  const size_t packet_1_size = 6;
  const std::vector<uint8_t> buffer = {
      // Packet 0
      0x00,  // handle + status parameters
      0x00,
      0x02,  // length
      0x03,  // payload
      0x04,
      // Packet 1
      0x00,  // handle + status parameters
      0x00,
      0x03,  // length
      0x05,  // payload
      0x06,
      0x07,
  };
  const size_t first_chunk_size = packet_0_size - 1;
  recomb.ProcessData({buffer.data(), first_chunk_size});
  EXPECT_EQ(output.size(), 0u);
  // Process all but the last byte of second packet
  recomb.ProcessData({buffer.data() + first_chunk_size, buffer.size() - first_chunk_size - 1});
  EXPECT_EQ(output.size(), 1u);
  EXPECT_EQ(output[0], std::vector(buffer.data(), buffer.data() + packet_0_size));
  // Process the end of the second packet.
  recomb.ProcessData({buffer.end() - 1, 1});
  EXPECT_EQ(output[1], std::vector(buffer.data() + packet_0_size,
                                   buffer.data() + packet_0_size + packet_1_size));
}

TEST(PacketReassemblerTest, ProcessBufferMuchLargerThanMaxPacketSize) {
  std::vector<std::vector<uint8_t>> output;
  bt_transport_usb::PacketReassembler</*kMaxPacketSize=*/2> recomb(
      /*length_param_index=*/0, /*header_size=*/1,
      [&](cpp20::span<const uint8_t> p) { output.emplace_back(p.begin(), p.end()); });

  const size_t packet_size = 2;
  const std::vector<uint8_t> buffer = {
      0x01, 0x00,  // Packet 0
      0x01, 0x01,  // Packet 1
      0x01, 0x02,  // Packet 2
      0x01, 0x03,  // Packet 3
      0x01, 0x04,  // Packet 4
  };
  recomb.ProcessData({buffer.data(), buffer.size()});
  EXPECT_EQ(output.size(), 5u);
  EXPECT_EQ(output[0], std::vector(buffer.data(), buffer.data() + packet_size));
  EXPECT_EQ(output[1], std::vector(buffer.data() + packet_size, buffer.data() + 2 * packet_size));
  EXPECT_EQ(output[2],
            std::vector(buffer.data() + 2 * packet_size, buffer.data() + 3 * packet_size));
  EXPECT_EQ(output[3],
            std::vector(buffer.data() + 3 * packet_size, buffer.data() + 4 * packet_size));
  EXPECT_EQ(output[4],
            std::vector(buffer.data() + 4 * packet_size, buffer.data() + 5 * packet_size));
}

}  // namespace
