// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "corrupt_blob.h"

#include <iterator>

#include <storage/buffer/owned_vmoid.h>
#include <zxtest/zxtest.h>

#include "src/lib/storage/block_client/cpp/fake_block_device.h"
#include "src/lib/storage/block_client/cpp/reader.h"
#include "src/storage/blobfs/format.h"
#include "src/storage/blobfs/mkfs.h"

namespace blobfs {
namespace {

using block_client::BlockDevice;
using block_client::FakeBlockDevice;

constexpr uint32_t kBlockSize = 512;
constexpr uint32_t kBlocksPerBlobfsBlock = kBlobfsBlockSize / kBlockSize;
constexpr uint32_t kNumBlocks = 400 * kBlocksPerBlobfsBlock;

// Re-implement BlockDevice around a reference to one so CorruptBlob can continue to take ownership
// of a block device and the tests can check the block device after corrupting a blob.
class ProxyBlockDevice : public BlockDevice {
 public:
  explicit ProxyBlockDevice(BlockDevice* inner) : inner_(inner) {}

  zx_status_t FifoTransaction(block_fifo_request_t* requests, size_t count) override {
    return inner_->FifoTransaction(requests, count);
  }
  zx::result<std::string> GetDevicePath() const override { return inner_->GetDevicePath(); }
  zx_status_t BlockGetInfo(fuchsia_hardware_block::wire::BlockInfo* out_info) const override {
    return inner_->BlockGetInfo(out_info);
  }
  zx_status_t BlockAttachVmo(const zx::vmo& vmo, storage::Vmoid* out_vmoid) override {
    return inner_->BlockAttachVmo(vmo, out_vmoid);
  }

  zx_status_t VolumeGetInfo(
      fuchsia_hardware_block_volume::wire::VolumeManagerInfo* out_manager,
      fuchsia_hardware_block_volume::wire::VolumeInfo* out_volume) const override {
    return inner_->VolumeGetInfo(out_manager, out_volume);
  }
  zx_status_t VolumeQuerySlices(const uint64_t* slices, size_t slices_count,
                                fuchsia_hardware_block_volume::wire::VsliceRange* out_ranges,
                                size_t* out_ranges_count) const override {
    return inner_->VolumeQuerySlices(slices, slices_count, out_ranges, out_ranges_count);
  }
  zx_status_t VolumeExtend(uint64_t offset, uint64_t length) override {
    return inner_->VolumeExtend(offset, length);
  }
  zx_status_t VolumeShrink(uint64_t offset, uint64_t length) override {
    return inner_->VolumeShrink(offset, length);
  }

 private:
  BlockDevice* inner_;
};

class MockBlockDevice : public FakeBlockDevice {
 public:
  MockBlockDevice(uint64_t block_count, uint32_t block_size)
      : FakeBlockDevice(block_count, block_size), block_size_(block_size) {}

  void WriteBlock(uint64_t block, uint64_t fs_block_size, const void* data);

 private:
  uint32_t block_size_;
};

void MockBlockDevice::WriteBlock(uint64_t block_num, uint64_t fs_block_size, const void* data) {
  zx::vmo vmo;
  ASSERT_OK(zx::vmo::create(fs_block_size, 0, &vmo));
  ASSERT_OK(vmo.write(data, 0, fs_block_size));

  storage::Vmoid vmoid;
  ASSERT_OK(BlockAttachVmo(vmo, &vmoid));

  block_fifo_request_t requests[2] = {};

  ASSERT_TRUE(fs_block_size % block_size_ == 0);
  requests[0].opcode = BLOCKIO_WRITE;
  requests[0].vmoid = vmoid.get();
  requests[0].length = static_cast<uint32_t>(fs_block_size / block_size_);
  requests[0].vmo_offset = 0;
  requests[0].dev_offset = block_num * fs_block_size / block_size_;

  requests[1].opcode = BLOCKIO_CLOSE_VMO;
  requests[1].vmoid = vmoid.TakeId();

  ASSERT_OK(FifoTransaction(requests, std::size(requests)));
}

std::unique_ptr<MockBlockDevice> CreateAndFormatDevice() {
  auto device = std::make_unique<MockBlockDevice>(kNumBlocks, kBlockSize);
  EXPECT_OK(FormatFilesystem(device.get(), FilesystemOptions{}));
  if (CURRENT_TEST_HAS_FAILURES()) {
    return nullptr;
  }
  return device;
}

class ZeroDiskTest : public zxtest::Test {
 public:
  void SetUp() override { device_ = std::make_unique<MockBlockDevice>(kNumBlocks, kBlockSize); }

 protected:
  std::unique_ptr<MockBlockDevice> device_;
};

class BlobfsDiskTest : public ZeroDiskTest {
 public:
  void SetUp() override {
    device_ = CreateAndFormatDevice();
    ASSERT_TRUE(device_);

    uint8_t block[kBlobfsBlockSize] = {};
    ASSERT_OK(block_client::Reader(*device_).Read(0, sizeof(block), &block));
    superblock_ = *reinterpret_cast<Superblock*>(block);
  }
  void WriteSuperblock() { device_->WriteBlock(0, sizeof(superblock_), &superblock_); }

 protected:
  std::unique_ptr<MockBlockDevice> device_;
  Superblock superblock_ = {};
};

TEST_F(ZeroDiskTest, StartStop) {}

TEST_F(ZeroDiskTest, FailsOnEmptyDisk) {
  BlobCorruptOptions options;
  ASSERT_EQ(ZX_ERR_INVALID_ARGS, CorruptBlob(std::move(device_), &options));
}

TEST_F(BlobfsDiskTest, StartStop) {}

TEST_F(BlobfsDiskTest, FailsOnNotFound) {
  BlobCorruptOptions options;
  ASSERT_EQ(ZX_ERR_NOT_FOUND, CorruptBlob(std::move(device_), &options));
}

TEST_F(BlobfsDiskTest, FailsOnUncleanDismount) {
  superblock_.flags &= ~kBlobFlagClean;
  ASSERT_NO_FAILURES(WriteSuperblock());

  BlobCorruptOptions options;
  ASSERT_EQ(ZX_ERR_BAD_STATE, CorruptBlob(std::move(device_), &options));
}

TEST_F(BlobfsDiskTest, SucceedsIfFirstNodeMatches) {
  ASSERT_NO_FAILURES(WriteSuperblock());

  BlobCorruptOptions options;
  ASSERT_OK(
      options.merkle.Parse("00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff"));

  Inode node = {};
  node.header.flags = kBlobFlagAllocated;
  options.merkle.CopyTo(node.merkle_root_hash);
  node.blob_size = 20;
  node.extent_count = 1;
  node.extents[0] = Extent(0, 1);

  uint8_t block[kBlobfsBlockSize] = {};
  auto node_block_num = NodeMapStartBlock(superblock_);
  block_client::Reader reader(*device_);
  ASSERT_OK(reader.Read(node_block_num * kBlobfsBlockSize, sizeof(block), block));
  reinterpret_cast<Inode*>(block)[0] = node;
  ASSERT_NO_FAILURES(device_->WriteBlock(node_block_num, sizeof(block), block));

  // Corrupt the blob, and ensure the data block for the blob is different after.
  auto data_block_num = DataStartBlock(superblock_);
  ASSERT_OK(reader.Read(data_block_num * kBlobfsBlockSize, sizeof(block), block));

  ASSERT_OK(CorruptBlob(std::make_unique<ProxyBlockDevice>(device_.get()), &options));

  uint8_t block_after[kBlobfsBlockSize] = {};
  ASSERT_OK(reader.Read(data_block_num * kBlobfsBlockSize, sizeof(block_after), block_after));
  ASSERT_BYTES_NE(block, block_after, 20);
}

TEST_F(BlobfsDiskTest, SucceedsIfLastNodeMatches) {
  ASSERT_NO_FAILURES(WriteSuperblock());

  BlobCorruptOptions options;
  ASSERT_OK(
      options.merkle.Parse("00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff"));

  Inode node = {};
  node.header.flags = kBlobFlagAllocated;
  options.merkle.CopyTo(node.merkle_root_hash);
  node.blob_size = 20;
  node.extent_count = 1;
  node.extents[0] = Extent(2, 1);

  uint8_t block[kBlobfsBlockSize] = {};
  auto node_block_num = NodeMapStartBlock(superblock_);
  block_client::Reader reader(*device_);
  ASSERT_OK(reader.Read(node_block_num * kBlobfsBlockSize, sizeof(block), block));
  reinterpret_cast<Inode*>(block)[kBlobfsInodesPerBlock - 1] = node;
  ASSERT_NO_FAILURES(device_->WriteBlock(node_block_num, sizeof(block), block));

  // Corrupt the blob, and ensure the data block for the blob is different after.
  auto data_block_num = DataStartBlock(superblock_) + 2;
  ASSERT_OK(reader.Read(data_block_num * kBlobfsBlockSize, sizeof(block), block));

  ASSERT_OK(CorruptBlob(std::make_unique<ProxyBlockDevice>(device_.get()), &options));

  uint8_t block_after[kBlobfsBlockSize] = {};
  ASSERT_OK(reader.Read(data_block_num * kBlobfsBlockSize, sizeof(block_after), block_after));
  ASSERT_BYTES_NE(block, block_after, 20);
}

}  // namespace
}  // namespace blobfs
