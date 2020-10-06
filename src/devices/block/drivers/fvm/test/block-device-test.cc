// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddktl/protocol/block.h>
#include <fvm/format.h>
#include <zxtest/zxtest.h>

#include "fvm-private.h"
#include "vpartition.h"

namespace {

using fvm::Header;
using fvm::VPartition;
using fvm::VPartitionManager;

constexpr size_t kFvmSliceSize = 8 * fvm::kBlockSize;
constexpr size_t kDiskSize = 64 * fvm::kBlockSize;
constexpr uint32_t kBlocksPerSlice = 128;

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

    // Supply the basic configuration to the FVM driver so it can make sense of slice requests.
    fvm::Header superblock =
        fvm::Header::FromDiskSize(fvm::kMaxUsablePartitions, kDiskSize, kFvmSliceSize);
    size_t metadata_vmo_size = superblock.GetDataStartOffset();
    fzl::OwnedVmoMapper metadata_mapper;
    ASSERT_OK(metadata_mapper.CreateAndMap(metadata_vmo_size, "fvm-metadata"));
    uint8_t* metadata_buffer = static_cast<uint8_t*>(metadata_mapper.start());

    // Copy the header to both copies of the metadata.
    memcpy(metadata_buffer, &superblock, sizeof(fvm::Header));
    memcpy(&metadata_buffer[superblock.GetSuperblockOffset(fvm::SuperblockType::kSecondary)],
           &superblock, sizeof(Header));
    device_->SetMetadataForTest(std::move(metadata_mapper));

    partition_ = std::make_unique<VPartition>(device_.get(), 1, block_op_size);

    ASSERT_EQ(kBlocksPerSlice, kFvmSliceSize / info.block_size);
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
