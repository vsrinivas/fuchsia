// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddktl/protocol/block.h>
#include <fvm/format.h>
#include <zxtest/zxtest.h>

#include "fvm-private.h"
#include "vpartition.h"

namespace {

using fvm::FormatInfo;
using fvm::Header;
using fvm::VPartition;
using fvm::VPartitionManager;

constexpr size_t kFvmSlizeSize = 8 * fvm::kBlockSize;
constexpr size_t kDiskSize = 64 * fvm::kBlockSize;
constexpr size_t kAllocTableSize = fvm::AllocTableLengthForDiskSize(kDiskSize, kFvmSlizeSize);
constexpr size_t kPartitionTableSize = fvm::PartitionTableLength(fvm::kMaxVPartitions);
constexpr uint32_t kBlocksPerSlice = 128;

// Initializes the header of an FVM volume.
Header MakeSuperBlock(size_t part_size, size_t part_table_size, size_t alloc_table_size) {
  Header superblock;
  superblock.fvm_partition_size = part_size;
  superblock.vpartition_table_size = part_table_size;
  superblock.allocation_table_size = alloc_table_size;
  superblock.slice_size = kFvmSlizeSize;
  superblock.version = fvm::kMagic;
  superblock.magic = fvm::kVersion;
  superblock.generation = 1;
  fvm::UpdateHash(&superblock, sizeof(Header));
  return superblock;
}

class FakeBlockDevice : public ddk::BlockImplProtocol<FakeBlockDevice> {
 public:
  FakeBlockDevice() : proto_({&block_impl_protocol_ops_, this}) {}

  block_impl_protocol_t* proto() { return &proto_; }

  // Block protocol:
  void BlockImplQuery(block_info_t* out_info, size_t* out_block_op_size) {
    *out_info = {};
    out_info->block_size = 512;
    out_info->block_count = kDiskSize / out_info->block_size;
    *out_block_op_size = sizeof(block_op_t);
  }

  void BlockImplQueue(block_op_t* operation, block_impl_queue_callback completion_cb,
                      void* cookie) {
    if (operation->trim.command == BLOCK_OP_TRIM) {
      num_calls_++;
      trim_length_ += operation->trim.length;
    }
    completion_cb(cookie, ZX_OK, operation);
  }

  int num_calls() const { return num_calls_; }
  uint32_t trim_length() const { return trim_length_; }

 private:
  block_impl_protocol_t proto_;
  int num_calls_ = 0;
  uint32_t trim_length_ = 0;
};

TEST(BlockDeviceTest, TrivialLifetime) {
  FakeBlockDevice block_device;
  block_info_t info;
  size_t block_op_size;
  block_device.BlockImplQuery(&info, &block_op_size);
  VPartitionManager device(nullptr, info, block_op_size, block_device.proto());

  VPartition partition(&device, 1, block_op_size);
}

class BlockDeviceTest : public zxtest::Test {
 public:
  void SetUp() override {
    block_info_t info;
    size_t block_op_size;
    block_device_.BlockImplQuery(&info, &block_op_size);
    device_ =
        std::make_unique<VPartitionManager>(nullptr, info, block_op_size, block_device_.proto());

    Header superblock = MakeSuperBlock(kDiskSize, kPartitionTableSize, kAllocTableSize);
    FormatInfo format_info(superblock);
    device_->SetFormatInfoForTest(format_info);

    partition_ = std::make_unique<VPartition>(device_.get(), 1, block_op_size);

    ASSERT_EQ(kBlocksPerSlice, kFvmSlizeSize / info.block_size);
  }

 protected:
  FakeBlockDevice block_device_;
  std::unique_ptr<VPartitionManager> device_;
  std::unique_ptr<VPartition> partition_;
};

// Verifies that simple TRIM commands are forwarded to the underlying device.
TEST_F(BlockDeviceTest, QueueTrimOneSlice) {
  const uint32_t kOperationLength = 20;
  partition_->SliceSetUnsafe(0, 0);

  block_op_t op = {};
  op.trim.command = BLOCK_OP_TRIM;
  op.trim.length = kOperationLength;
  op.trim.offset_dev = kBlocksPerSlice / 2;

  partition_->BlockImplQueue(
      &op, [](void*, zx_status_t status, block_op_t*) {}, nullptr);
  EXPECT_EQ(1, block_device_.num_calls());
  EXPECT_EQ(kOperationLength, block_device_.trim_length());
}

// Verifies that TRIM commands that span slices are forwarded to the underlying device.
TEST_F(BlockDeviceTest, QueueTrimConsecutiveSlices) {
  const uint32_t kOperationLength = 20;
  partition_->SliceSetUnsafe(0, 0);
  partition_->SliceSetUnsafe(1, 1);

  block_op_t op = {};
  op.trim.command = BLOCK_OP_TRIM;
  op.trim.length = kOperationLength;
  op.trim.offset_dev = kBlocksPerSlice - kOperationLength / 2;

  partition_->BlockImplQueue(
      &op, [](void*, zx_status_t status, block_op_t*) {}, nullptr);
  EXPECT_EQ(1, block_device_.num_calls());
  EXPECT_EQ(kOperationLength, block_device_.trim_length());
}

// Verifies that TRIM commands spanning non-consecutive slices are forwarded to the
// underlying device.
TEST_F(BlockDeviceTest, QueueTrimDisjointSlices) {
  const uint32_t kOperationLength = 20;
  partition_->SliceSetUnsafe(1, 1);
  partition_->SliceSetUnsafe(2, 5);

  block_op_t op = {};
  op.trim.command = BLOCK_OP_TRIM;
  op.trim.length = kOperationLength;
  op.trim.offset_dev = kBlocksPerSlice * 2 - kOperationLength / 2;

  partition_->BlockImplQueue(
      &op, [](void*, zx_status_t status, block_op_t*) {}, nullptr);
  EXPECT_EQ(2, block_device_.num_calls());
  EXPECT_EQ(kOperationLength, block_device_.trim_length());
}

}  // namespace
