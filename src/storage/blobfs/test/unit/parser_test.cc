// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "inspector/parser.h"

#include <blobfs/format.h>
#include <storage/buffer/array_buffer.h>
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
    ASSERT_EQ(i, out_inode.blob_size);
    ASSERT_EQ(expected_block_count, out_inode.block_count);
  }
}

TEST(InspectorParser, WriteBitmapElement) {
  uint32_t start_block = 0;
  uint32_t block_length = 1;
  storage::ArrayBuffer device(block_length, kBlobfsBlockSize);
  memset(device.Data(start_block), 0xFF, device.capacity() * device.BlockSize());

  for (uint32_t i = 0; i < block_length * kBlobfsBlockSize * CHAR_BIT; ++i) {
    ASSERT_TRUE(GetBitmapElement(&device, i));
  }

  // Write the |write_element| bit to false.
  uint64_t write_element = 25;
  bool write_value = false;
  WriteBitmapElement(&device, write_value, write_element);

  for (uint32_t i = 0; i < block_length * kBlobfsBlockSize * CHAR_BIT; ++i) {
    bool is_set = (i == write_element) ? write_value : !write_value;
    ASSERT_EQ(is_set, GetBitmapElement(&device, i));
  }

  // Write the bit back to true.
  WriteBitmapElement(&device, true, write_element);

  for (uint32_t i = 0; i < block_length * kBlobfsBlockSize * CHAR_BIT; ++i) {
    ASSERT_TRUE(GetBitmapElement(&device, i));
  }
}

TEST(InspectorParser, WriteSingleInodeElement) {
  uint32_t block_length = 2;
  storage::ArrayBuffer device(block_length, kBlobfsBlockSize);
  memset(device.Data(0), 0x00, block_length * kBlobfsBlockSize);

  uint32_t expected_block_count = 42;
  uint32_t inode_count = block_length * kBlobfsInodesPerBlock;

  // Sanity check zeroed out ok.
  for (uint32_t i = 0; i < inode_count; ++i) {
    Inode out_inode = GetInodeElement(&device, i);
    ASSERT_EQ(0, out_inode.blob_size);
    ASSERT_EQ(0, out_inode.block_count);
  }

  // Test writing a single inode.
  Inode inode = {};
  inode.blob_size = 0;
  inode.block_count = expected_block_count;
  WriteInodeElement(&device, inode, 0);
  Inode out_inode = GetInodeElement(&device, 0);

  ASSERT_EQ(inode.blob_size, out_inode.blob_size);
  ASSERT_EQ(inode.block_count, out_inode.block_count);

  // Make sure rest of device is not set.
  for (uint64_t i = 1; i < inode_count; ++i) {
    Inode out_inode = GetInodeElement(&device, i);
    ASSERT_EQ(0, out_inode.blob_size);
    ASSERT_EQ(0, out_inode.block_count);
  }
}

TEST(InspectorParser, WriteManyInodeElements) {
  uint32_t block_length = 2;
  storage::ArrayBuffer device(block_length, kBlobfsBlockSize);
  memset(device.Data(0), 0x00, block_length * kBlobfsBlockSize);

  uint32_t expected_block_count = 42;
  uint32_t inode_count = block_length * kBlobfsInodesPerBlock;

  // Sanity check zeroed out ok.
  for (uint32_t i = 0; i < inode_count; ++i) {
    Inode out_inode = GetInodeElement(&device, i);
    ASSERT_EQ(0, out_inode.blob_size);
    ASSERT_EQ(0, out_inode.block_count);
  }

  // Test setting all the inodes.
  for (uint64_t i = 0; i < inode_count; ++i) {
    Inode inode = {};
    inode.blob_size = i;
    inode.block_count = expected_block_count;
    WriteInodeElement(&device, inode, i);
  }
  for (uint64_t i = 0; i < inode_count; ++i) {
    Inode out_inode = GetInodeElement(&device, i);
    ASSERT_EQ(i, out_inode.blob_size);
    ASSERT_EQ(expected_block_count, out_inode.block_count);
  }
}

}  // namespace
}  // namespace blobfs
