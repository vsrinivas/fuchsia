// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/minfs/inspector/parser.h"

#include <gtest/gtest.h>
#include <storage/buffer/array_buffer.h>

#include "src/storage/minfs/format.h"

namespace minfs {
namespace {

TEST(InspectorParser, ParseSuperblock) {
  uint64_t start_block = 0;
  uint64_t block_length = 1;
  Superblock superblock;
  superblock.magic0 = kMinfsMagic0;
  superblock.magic1 = kMinfsMagic1;
  superblock.dat_block = 1234;

  storage::ArrayBuffer device(block_length, kMinfsBlockSize);
  auto device_sb = reinterpret_cast<Superblock*>(device.Data(start_block));
  *device_sb = superblock;

  Superblock out_superblock = GetSuperblock(&device);
  ASSERT_EQ(superblock.magic0, out_superblock.magic0);
  ASSERT_EQ(superblock.magic1, out_superblock.magic1);
  ASSERT_EQ(superblock.inode_size, out_superblock.inode_size);
  ASSERT_EQ(superblock.dat_block, out_superblock.dat_block);
}

TEST(InspectorParser, ParseInodeBitmap) {
  uint32_t start_block = 0;
  uint32_t block_length = 1;
  storage::ArrayBuffer device(block_length, kMinfsBlockSize);
  memset(device.Data(start_block), 0xAA, device.capacity() * device.BlockSize());

  for (uint32_t i = 0; i < block_length * kMinfsBlockSize * CHAR_BIT; ++i) {
    ASSERT_EQ(i % 2 != 0, GetBitmapElement(&device, i));
  }
}

TEST(InspectorParser, ParseInodeTable) {
  uint32_t block_length = 2;
  storage::ArrayBuffer device(block_length, kMinfsBlockSize);

  uint32_t inode_count = 0;
  for (uint32_t block_offset = 0; block_offset < block_length; ++block_offset) {
    auto inodes = reinterpret_cast<Inode*>(device.Data(block_offset));
    for (uint32_t inode_offset = 0; inode_offset < kMinfsInodesPerBlock; ++inode_offset) {
      inodes[inode_offset].magic = kMinfsMagicFile;
      inodes[inode_offset].seq_num = inode_count;
      inode_count++;
    }
  }
  for (uint32_t i = 0; i < inode_count; ++i) {
    Inode out_inode = GetInodeElement(&device, i);
    ASSERT_EQ(out_inode.magic, kMinfsMagicFile);
    ASSERT_EQ(out_inode.seq_num, i);
  }
}

}  // namespace
}  // namespace minfs
