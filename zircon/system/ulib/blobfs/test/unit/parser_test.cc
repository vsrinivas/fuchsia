// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "inspector/parser.h"

#include <blobfs/format.h>
#include <buffer/test_support/array_buffer.h>
#include <fs/transaction/block_transaction.h>
#include <zxtest/zxtest.h>

namespace blobfs {
namespace {

TEST(InspectorParser, ParseSuperblock) {
  uint64_t start_block = 0;
  uint64_t block_length = 1;
  Superblock superblock;
  superblock.magic0 = kBlobfsMagic0;
  superblock.magic1 = kBlobfsMagic1;
  superblock.alloc_block_count = 1234;

  storage::ArrayBuffer device(block_length, kBlobfsBlockSize);
  auto device_sb = reinterpret_cast<Superblock*>(device.Data(start_block));
  *device_sb = superblock;

  Superblock out_superblock = GetSuperblock(&device);
  ASSERT_EQ(superblock.magic0, out_superblock.magic0);
  ASSERT_EQ(superblock.magic1, out_superblock.magic1);
  ASSERT_EQ(superblock.blob_header_next, out_superblock.blob_header_next);
  ASSERT_EQ(superblock.alloc_block_count, out_superblock.alloc_block_count);
}

TEST(InspectorParser, ParseBitmap) {
  uint32_t start_block = 0;
  uint32_t block_length = 1;
  storage::ArrayBuffer device(block_length, kBlobfsBlockSize);
  memset(device.Data(start_block), 0xAA, device.capacity() * device.BlockSize());

  for (uint32_t i = 0; i < block_length * kBlobfsBlockSize * CHAR_BIT; ++i) {
    ASSERT_EQ(i % 2 != 0, GetBitmapElement(&device, i));
  }
}

TEST(InspectorParser, ParseInodeTable) {
  uint32_t block_length = 2;
  storage::ArrayBuffer device(block_length, kBlobfsBlockSize);

  uint32_t expected_block_count = 42;
  uint32_t inode_count = 0;
  for (uint32_t block_offset = 0; block_offset < block_length; ++block_offset) {
    auto inodes = reinterpret_cast<Inode*>(device.Data(block_offset));
    for (uint32_t inode_offset = 0; inode_offset < kBlobfsInodesPerBlock; ++inode_offset) {
      inodes[inode_offset].blob_size = inode_count;
      inodes[inode_offset].block_count = expected_block_count;
      inode_count++;
    }
  }
  for (uint32_t i = 0; i < inode_count; ++i) {
    Inode out_inode = GetInodeElement(&device, i);
    ASSERT_EQ(out_inode.blob_size, i);
    ASSERT_EQ(out_inode.block_count, expected_block_count);
  }
}

}  // namespace
}  // namespace blobfs
