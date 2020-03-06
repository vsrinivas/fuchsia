// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "buffer.h"

#include <lib/fzl/vmo-mapper.h>

#include <gtest/gtest.h>

namespace network {
namespace tun {
namespace testing {

constexpr uint64_t kVmoSize = ZX_PAGE_SIZE;
constexpr uint8_t kVmoId = 0x06;

class BufferTest : public ::testing::Test {
  void SetUp() override {
    zx::vmo vmo;
    ASSERT_EQ(zx::vmo::create(kVmoSize, 0, &vmo), ZX_OK);
    ASSERT_EQ(vmos_.RegisterVmo(kVmoId, std::move(vmo)), ZX_OK);
  }

 public:
  void MintVmo(size_t offset, size_t len) {
    uint8_t val = 0;
    while (len--) {
      ASSERT_EQ(vmos_.Write(kVmoId, offset, 1, &val), ZX_OK);
      offset++;
      val++;
    }
  }

  void MintVmo(const buffer_region_t& region) { MintVmo(region.offset, region.length); }

  std::vector<uint8_t> ReadVmo(const buffer_region_t& region) {
    std::vector<uint8_t> ret;
    ret.reserve(region.length);
    EXPECT_EQ(vmos_.Read(kVmoId, region.offset, region.length, std::back_inserter(ret)), ZX_OK);
    return ret;
  }

 protected:
  VmoStore vmos_;
};

TEST_F(BufferTest, TestBufferBuildTx) {
  buffer_region_t regions[2];
  regions[0].offset = 10;
  regions[0].length = 5;
  regions[1].offset = 100;
  regions[1].length = 3;
  MintVmo(regions[0]);
  MintVmo(regions[1]);
  tx_buffer_t tx;
  tx.id = 1;
  tx.virtual_mem.vmo_id = kVmoId;
  tx.virtual_mem.parts_count = 2;
  tx.virtual_mem.parts_list = regions;
  tx.head_length = 0;
  tx.tail_length = 0;
  tx.meta.frame_type = static_cast<uint8_t>(fuchsia::hardware::network::FrameType::ETHERNET);
  tx.meta.info_type = static_cast<uint32_t>(fuchsia::hardware::network::InfoType::NO_INFO);
  tx.meta.flags = static_cast<uint32_t>(fuchsia::hardware::network::TxFlags::TX_ACCEL_0);
  auto b = vmos_.MakeTxBuffer(&tx, true);
  EXPECT_EQ(b.id(), tx.id);
  EXPECT_EQ(b.frame_type(), fuchsia::hardware::network::FrameType::ETHERNET);
  auto meta = b.TakeMetadata();
  EXPECT_EQ(meta->info_type, fuchsia::hardware::network::InfoType::NO_INFO);
  EXPECT_TRUE(meta->info.empty());
  EXPECT_EQ(meta->flags, static_cast<uint32_t>(fuchsia::hardware::network::TxFlags::TX_ACCEL_0));
  std::vector<uint8_t> data;
  ASSERT_EQ(b.Read(&data), ZX_OK);
  EXPECT_EQ(data, std::vector<uint8_t>({0x00, 0x01, 0x02, 0x03, 0x04, 0x00, 0x01, 0x02}));
}

TEST_F(BufferTest, TestBufferBuildRx) {
  buffer_region_t parts[2];
  rx_space_buffer_t space;
  space.id = 1;
  space.virtual_mem.vmo_id = kVmoId;
  space.virtual_mem.parts_count = 2;
  space.virtual_mem.parts_list = parts;

  parts[0].offset = 10;
  parts[0].length = 5;
  parts[1].offset = 100;
  parts[1].length = 3;

  auto b = vmos_.MakeRxSpaceBuffer(&space);

  EXPECT_EQ(b.id(), space.id);
  std::vector<uint8_t> wr_data({0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x00, 0x01, 0x02});
  ASSERT_EQ(b.Write(wr_data), ZX_OK);
  EXPECT_EQ(ReadVmo(parts[0]), std::vector<uint8_t>({0xAA, 0xBB, 0xCC, 0xDD, 0xEE}));
  EXPECT_EQ(ReadVmo(parts[1]), std::vector<uint8_t>({0x00, 0x01, 0x02}));
}

TEST_F(BufferTest, CopyBuffer) {
  buffer_region_t tx_parts[3];
  tx_parts[0].offset = 0;
  tx_parts[0].length = 5;
  tx_parts[1].offset = 10;
  tx_parts[1].length = 3;
  tx_parts[2].offset = 20;
  tx_parts[2].length = 2;
  MintVmo(tx_parts[0]);
  MintVmo(tx_parts[1]);
  MintVmo(tx_parts[0]);
  MintVmo(tx_parts[2]);
  tx_buffer_t tx;
  tx.id = 1;
  tx.virtual_mem.vmo_id = kVmoId;
  tx.virtual_mem.parts_count = 3;
  tx.virtual_mem.parts_list = tx_parts;

  auto b_tx = vmos_.MakeTxBuffer(&tx, false);

  buffer_region_t rx_parts[3];
  rx_space_buffer_t space;
  space.id = 1;
  space.virtual_mem.vmo_id = kVmoId;
  space.virtual_mem.parts_count = 3;
  space.virtual_mem.parts_list = rx_parts;

  rx_parts[0].offset = 100;
  rx_parts[0].length = 3;
  rx_parts[1].offset = 110;
  rx_parts[1].length = 5;
  rx_parts[2].offset = 120;
  rx_parts[2].length = 100;

  auto b_rx = vmos_.MakeRxSpaceBuffer(&space);

  size_t total;
  ASSERT_EQ(b_rx.CopyFrom(&b_tx, &total), ZX_OK);
  EXPECT_EQ(total, 10ul);

  EXPECT_EQ(ReadVmo(rx_parts[0]), std::vector<uint8_t>({0x00, 0x01, 0x02}));
  EXPECT_EQ(ReadVmo(rx_parts[1]), std::vector<uint8_t>({0x03, 0x04, 0x00, 0x01, 0x02}));
  EXPECT_EQ(ReadVmo(buffer_region_t{.offset = rx_parts[2].offset, .length = 2}),
            std::vector<uint8_t>({0x00, 0x01}));
}

TEST_F(BufferTest, WriteFailure) {
  buffer_region_t parts;
  rx_space_buffer_t space;
  space.id = 1;
  space.virtual_mem.vmo_id = kVmoId;
  space.virtual_mem.parts_count = 1;
  space.virtual_mem.parts_list = &parts;

  parts.offset = 10;
  parts.length = 3;

  {
    // Write more than buffer's length is invalid.
    auto b = vmos_.MakeRxSpaceBuffer(&space);
    ASSERT_EQ(b.Write({0x01, 0x02, 0x03, 0x04}), ZX_ERR_OUT_OF_RANGE);
  }
  {
    // A buffer that doesn't fit its VMO is invalid.
    parts.offset = kVmoSize;
    auto b = vmos_.MakeRxSpaceBuffer(&space);
    ASSERT_EQ(b.Write({0x01}), ZX_ERR_OUT_OF_RANGE);
  }
  {
    // A buffer with an invalid vmo_id is invalid.
    space.virtual_mem.vmo_id = kVmoId + 1;
    auto b = vmos_.MakeRxSpaceBuffer(&space);
    ASSERT_EQ(b.Write({0x01}), ZX_ERR_NOT_FOUND);
  }
}

TEST_F(BufferTest, ReadFailure) {
  buffer_region_t parts;
  tx_buffer_t tx_buffer;
  tx_buffer.id = 1;
  tx_buffer.virtual_mem.vmo_id = kVmoId;
  tx_buffer.virtual_mem.parts_count = 1;
  tx_buffer.virtual_mem.parts_list = &parts;

  parts.length = 3;
  std::vector<uint8_t> data;

  {
    // A buffer that doesn't fit its VMO is invalid.
    parts.offset = kVmoSize;
    auto b = vmos_.MakeTxBuffer(&tx_buffer, false);
    ASSERT_EQ(b.Read(&data), ZX_ERR_OUT_OF_RANGE);
  }
  {
    // A buffer with an invalid vmo_id is invalid.
    tx_buffer.virtual_mem.vmo_id = kVmoId + 1;
    auto b = vmos_.MakeTxBuffer(&tx_buffer, false);
    ASSERT_EQ(b.Read(&data), ZX_ERR_NOT_FOUND);
  }
}

TEST_F(BufferTest, CopyFailure) {
  // Source region is out of range.
  ASSERT_EQ(VmoStore::Copy(&vmos_, kVmoId, kVmoSize, &vmos_, kVmoId, 0, 10), ZX_ERR_OUT_OF_RANGE);
  // Destination region is out of range,
  ASSERT_EQ(VmoStore::Copy(&vmos_, kVmoId, 0, &vmos_, kVmoId, kVmoSize, 10), ZX_ERR_OUT_OF_RANGE);
  // Source region is has bad id.
  ASSERT_EQ(VmoStore::Copy(&vmos_, kVmoId + 1, 0, &vmos_, kVmoId, 0, 10), ZX_ERR_NOT_FOUND);
  // Destination region is has bad id.
  ASSERT_EQ(VmoStore::Copy(&vmos_, kVmoId, 0, &vmos_, kVmoId + 1, 0, 10), ZX_ERR_NOT_FOUND);
}

}  // namespace testing
}  // namespace tun
}  // namespace network
