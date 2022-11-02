// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/blobfs/format.h"

#include <gtest/gtest.h>

#include "src/lib/storage/block_client/cpp/fake_block_device.h"
#include "src/lib/storage/block_client/cpp/reader.h"
#include "src/storage/blobfs/blobfs.h"
#include "src/storage/blobfs/mkfs.h"
#include "src/storage/blobfs/test/blobfs_test_setup.h"

namespace blobfs {
namespace {

using block_client::FakeBlockDevice;
using block_client::FakeFVMBlockDevice;

zx_status_t CheckMountability(std::unique_ptr<BlockDevice> device) {
  MountOptions options = {};
  options.writability = Writability::ReadOnlyFilesystem;

  BlobfsTestSetup setup;
  return setup.Mount(std::move(device), options);
}

void CheckDefaultInodeCount(std::unique_ptr<BlockDevice> device) {
  BlobfsTestSetup setup;
  ASSERT_EQ(ZX_OK, setup.Mount(std::move(device)));
  ASSERT_GE(setup.blobfs()->Info().inode_count, kBlobfsDefaultInodeCount);
}

void CheckDefaultJournalBlocks(std::unique_ptr<BlockDevice> device) {
  BlobfsTestSetup setup;
  ASSERT_EQ(ZX_OK, setup.Mount(std::move(device)));
  ASSERT_GE(setup.blobfs()->Info().journal_block_count, kMinimumJournalBlocks);
}

// Formatting filesystems should fail on devices that cannot be written.
TEST(FormatFilesystemTest, CannotFormatReadOnlyDevice) {
  auto device = std::make_unique<FakeBlockDevice>(1 << 20, 512);
  device->SetInfoFlags(fuchsia_hardware_block::wire::kFlagReadonly);
  ASSERT_EQ(ZX_ERR_ACCESS_DENIED, FormatFilesystem(device.get(), FilesystemOptions{}));
}

// Formatting filesystems should fail on devices that don't contain any blocks.
TEST(FormatFilesystemTest, CannotFormatEmptyDevice) {
  auto device = std::make_unique<FakeBlockDevice>(0, 0);
  ASSERT_EQ(ZX_ERR_NO_SPACE, FormatFilesystem(device.get(), FilesystemOptions{}));
}

// Formatting filesystems should fail on devices that aren't empty, but are
// still too small to contain a filesystem.
TEST(FormatFilesystemTest, CannotFormatSmallDevice) {
  auto device = std::make_unique<FakeBlockDevice>(1, 512);
  ASSERT_EQ(ZX_ERR_NO_SPACE, FormatFilesystem(device.get(), FilesystemOptions{}));
}

// Formatting filesystems should fail on devices which have a block size that
// does not cleanly divide the blobfs block size.
TEST(FormatFilesystemTest, CannotFormatDeviceWithNonDivisorBlockSize) {
  const uint64_t kBlockCount = 1 << 20;
  uint64_t kBlockSize = 511;
  EXPECT_NE(kBlobfsBlockSize % kBlockSize, 0ul) << "Expected non-divisor block size";
  auto device = std::make_unique<FakeBlockDevice>(kBlockCount, kBlockSize);
  ASSERT_EQ(ZX_ERR_IO_INVALID, FormatFilesystem(device.get(), FilesystemOptions{}));
}

// Calculates the smallest number of blobfs blocks to generate a valid blobfs format.
constexpr uint64_t MinimumFilesystemBlocks() {
  const uint64_t kSuperBlockBlocks = 1;
  const uint64_t kInodeBlocks = sizeof(Inode) * kBlobfsDefaultInodeCount / kBlobfsBlockSize;
  const uint64_t kJournalBlocks = kMinimumJournalBlocks;
  const uint64_t kDataBlocks = kMinimumDataBlocks;
  const uint64_t kBlockMapBlocks = fbl::round_up(kDataBlocks, kBlobfsBlockBits) / kBlobfsBlockBits;

  return kSuperBlockBlocks + kInodeBlocks + kJournalBlocks + kDataBlocks + kBlockMapBlocks;
}

// Blobfs can be formatted on the smallest possible device.
TEST(FormatFilesystemTest, FormatNonFVMSmallestDevice) {
  const uint32_t kBlockSize = 512;
  const uint64_t kDiskBlockRatio = kBlobfsBlockSize / kBlockSize;
  const uint64_t kBlockCount = kDiskBlockRatio * MinimumFilesystemBlocks();

  // Smallest possible device.
  {
    auto device = std::make_unique<FakeBlockDevice>(kBlockCount, kBlockSize);
    ASSERT_EQ(FormatFilesystem(device.get(), FilesystemOptions{}), ZX_OK);
    ASSERT_EQ(CheckMountability(std::move(device)), ZX_OK);
  }

  // One block smaller than the smallest possible device.
  {
    auto device = std::make_unique<FakeBlockDevice>(kBlockCount - 1, kBlockSize);
    ASSERT_EQ(ZX_ERR_NO_SPACE, FormatFilesystem(device.get(), FilesystemOptions{}));
  }
}

// Calculates the smallest number of blobfs slices to generate a valid blobfs format.
uint64_t MinimumFilesystemSlices(uint64_t kSliceSize) {
  uint64_t kBlocksPerSlice = kSliceSize / kBlobfsBlockSize;
  auto BlocksToSlices = [kBlocksPerSlice](uint64_t blocks) {
    return fbl::round_up(blocks, kBlocksPerSlice) / kBlocksPerSlice;
  };

  const uint64_t kSuperBlockSlices = BlocksToSlices(1);
  const uint64_t kInodeSlices = BlocksToSlices(kBlobfsDefaultInodeCount / kBlobfsInodesPerBlock);
  const uint64_t kJournalSlices = BlocksToSlices(kMinimumJournalBlocks);
  const uint64_t kDataSlices = BlocksToSlices(kMinimumDataBlocks);
  const uint64_t kBlockMapSlices = BlocksToSlices(BlocksRequiredForBits(kMinimumDataBlocks));

  return kSuperBlockSlices + kInodeSlices + kJournalSlices + kDataSlices + kBlockMapSlices;
}

TEST(FormatFilesystemTest, FormatFVMSmallestDevice) {
  const uint32_t kBlockSize = 512;
  const uint64_t kSliceSize = kBlobfsBlockSize * 8;
  const uint64_t kSliceCount = MinimumFilesystemSlices(kSliceSize);
  const uint64_t kBlockCount = kSliceCount * kSliceSize / kBlockSize;

  // Smallest possible device.
  {
    auto device =
        std::make_unique<FakeFVMBlockDevice>(kBlockCount, kBlockSize, kSliceSize, kSliceCount);
    ASSERT_EQ(FormatFilesystem(device.get(), FilesystemOptions{}), ZX_OK);
    ASSERT_EQ(CheckMountability(std::move(device)), ZX_OK);
  }

  // One slice smaller than the smallest possible device.
  {
    auto device =
        std::make_unique<FakeFVMBlockDevice>(kBlockCount, kBlockSize, kSliceSize, kSliceCount - 1);
    ASSERT_EQ(ZX_ERR_NO_SPACE, FormatFilesystem(device.get(), FilesystemOptions{}));
  }
}

// Blobfs can be formatted on slightly larger devices as well.
TEST(FormatFilesystemTest, FormatNonFVMDevice) {
  const uint64_t kBlockCount = 1 << 20;
  const uint32_t kBlockSize = 512;
  auto device = std::make_unique<FakeBlockDevice>(kBlockCount, kBlockSize);
  ASSERT_EQ(FormatFilesystem(device.get(), FilesystemOptions{}), ZX_OK);
  ASSERT_EQ(CheckMountability(std::move(device)), ZX_OK);
}

TEST(FormatFilesystemTest, FormatFVMDevice) {
  const uint64_t kBlockCount = 1 << 20;
  const uint32_t kBlockSize = 512;
  const uint64_t kSliceSize = kBlobfsBlockSize * 8;
  const uint64_t kSliceCount = 1028;
  auto device =
      std::make_unique<FakeFVMBlockDevice>(kBlockCount, kBlockSize, kSliceSize, kSliceCount);
  ASSERT_EQ(FormatFilesystem(device.get(), FilesystemOptions{}), ZX_OK);
  ASSERT_EQ(CheckMountability(std::move(device)), ZX_OK);
}

// Blobfs can be formatted on devices that have "trailing device block(s)" that
// cannot be fully addressed by blobfs blocks.
TEST(FormatFilesystemTest, FormatNonFVMDeviceWithTrailingDiskBlock) {
  const uint64_t kBlockCount = (1 << 20) + 1;
  const uint32_t kBlockSize = 512;
  auto device = std::make_unique<FakeBlockDevice>(kBlockCount, kBlockSize);
  ASSERT_EQ(FormatFilesystem(device.get(), FilesystemOptions{}), ZX_OK);
  ASSERT_EQ(CheckMountability(std::move(device)), ZX_OK);
}

TEST(FormatFilesystemTest, FormatFVMDeviceWithTrailingDiskBlock) {
  const uint64_t kBlockCount = (1 << 20) + 1;
  const uint32_t kBlockSize = 512;
  const uint64_t kSliceSize = kBlobfsBlockSize * 8;
  const uint64_t kSliceCount = 1028;
  auto device =
      std::make_unique<FakeFVMBlockDevice>(kBlockCount, kBlockSize, kSliceSize, kSliceCount);
  ASSERT_EQ(FormatFilesystem(device.get(), FilesystemOptions{}), ZX_OK);
  ASSERT_EQ(CheckMountability(std::move(device)), ZX_OK);
}

// Blobfs can be formatted on devices that have block sizes up to and including
// the blobfs block size itself.
TEST(FormatFilesystemTest, FormatNonFVMDeviceWithLargestBlockSize) {
  const uint64_t kBlockCount = 1 << 20;
  const uint32_t kBlockSize = kBlobfsBlockSize;
  auto device = std::make_unique<FakeBlockDevice>(kBlockCount, kBlockSize);
  ASSERT_EQ(FormatFilesystem(device.get(), FilesystemOptions{}), ZX_OK);
  ASSERT_EQ(CheckMountability(std::move(device)), ZX_OK);
}

TEST(FormatFilesystemTest, FormatFVMDeviceWithLargestBlockSize) {
  const uint64_t kBlockCount = 1 << 20;
  const uint32_t kBlockSize = kBlobfsBlockSize;
  const uint64_t kSliceSize = kBlobfsBlockSize * 8;
  const uint64_t kSliceCount = 1028;
  auto device =
      std::make_unique<FakeFVMBlockDevice>(kBlockCount, kBlockSize, kSliceSize, kSliceCount);
  ASSERT_EQ(FormatFilesystem(device.get(), FilesystemOptions{}), ZX_OK);
  ASSERT_EQ(CheckMountability(std::move(device)), ZX_OK);
}

// Blobfs cannot be formatted on devices that have block sizes larger than the
// blobfs block size itself.
TEST(FormatFilesystemTest, FormatNonFVMDeviceWithTooLargeBlockSize) {
  const uint64_t kBlockCount = 1 << 20;
  const uint32_t kBlockSize = kBlobfsBlockSize * 2;
  auto device = std::make_unique<FakeBlockDevice>(kBlockCount, kBlockSize);
  ASSERT_EQ(ZX_ERR_IO_INVALID, FormatFilesystem(device.get(), FilesystemOptions{}));
  ASSERT_EQ(ZX_ERR_INVALID_ARGS, CheckMountability(std::move(device)));
}

TEST(FormatFilesystemTest, FormatFVMDeviceWithTooLargeBlockSize) {
  const uint64_t kBlockCount = 1 << 20;
  const uint32_t kBlockSize = kBlobfsBlockSize * 2;
  const uint64_t kSliceSize = kBlobfsBlockSize * 8;
  const uint64_t kSliceCount = 1028;
  auto device =
      std::make_unique<FakeFVMBlockDevice>(kBlockCount, kBlockSize, kSliceSize, kSliceCount);
  ASSERT_EQ(ZX_ERR_IO_INVALID, FormatFilesystem(device.get(), FilesystemOptions{}));
  ASSERT_EQ(ZX_ERR_INVALID_ARGS, CheckMountability(std::move(device)));
}

// Validates that a formatted filesystem, can't be mounted as writable on a read-only device.
TEST(FormatFilesystemTest, DeviceNotWritableAutoConvertReadonly) {
  const uint64_t kBlockCount = 1 << 20;
  const uint32_t kBlockSize = kBlobfsBlockSize;
  auto device = std::make_unique<FakeBlockDevice>(kBlockCount, kBlockSize);
  ASSERT_EQ(FormatFilesystem(device.get(), FilesystemOptions{}), ZX_OK);
  device->SetInfoFlags(fuchsia_hardware_block::wire::kFlagReadonly);

  MountOptions mount_options = {};
  mount_options.writability = Writability::Writable;

  BlobfsTestSetup setup;
  ASSERT_EQ(ZX_ERR_ACCESS_DENIED, setup.Mount(std::move(device), mount_options));
}

// Validates that a formatted filesystem mounted as writable with a journal cannot be mounted on a
// read-only device. This "auto-conversion" is disabled because journal replay is necessary
// to guarantee filesystem correctness, which involves writeback.
TEST(FormatFilesystemTest, FormatDeviceWithJournalCannotAutoConvertReadonly) {
  const uint64_t kBlockCount = 1 << 20;
  const uint32_t kBlockSize = kBlobfsBlockSize;
  auto device = std::make_unique<FakeBlockDevice>(kBlockCount, kBlockSize);
  ASSERT_EQ(FormatFilesystem(device.get(), FilesystemOptions{}), ZX_OK);
  device->SetInfoFlags(fuchsia_hardware_block::wire::kFlagReadonly);

  MountOptions options = {};
  options.writability = Writability::Writable;

  BlobfsTestSetup setup;
  ASSERT_EQ(ZX_ERR_ACCESS_DENIED, setup.Mount(std::move(device), options));
}

// After formatting a filesystem with block size valid block size N, mounting on
// a device with an invalid block size should fail.
TEST(FormatFilesystemTest, CreateBlobfsFailureOnUnalignedBlockSize) {
  const uint64_t kBlockCount = 1 << 20;
  const uint32_t kBlockSize = 512;
  auto device = std::make_unique<FakeBlockDevice>(kBlockCount, kBlockSize);
  ASSERT_EQ(FormatFilesystem(device.get(), FilesystemOptions{}), ZX_OK);
  device->SetBlockSize(kBlockSize + 1);
  ASSERT_EQ(ZX_ERR_INVALID_ARGS, CheckMountability(std::move(device)));
}

// After formatting a filesystem with block size valid block count N, mounting
// on a device less M blocks (for M < N) should fail.
TEST(FormatFilesystemTest, CreateBlobfsFailureWithLessBlocks) {
  const uint64_t kBlockCount = 1 << 20;
  const uint32_t kBlockSize = 512;
  auto device = std::make_unique<FakeBlockDevice>(kBlockCount, kBlockSize);
  ASSERT_EQ(FormatFilesystem(device.get(), FilesystemOptions{}), ZX_OK);
  device->SetBlockCount(kBlockCount - 1);
  ASSERT_EQ(ZX_ERR_INVALID_ARGS, CheckMountability(std::move(device)));
}

// After formatting a filesystem with block size valid block count N, mounting
// on a device less M blocks (for M > N) should succeed.
TEST(FormatFilesystemTest, CreateBlobfsSuccessWithMoreBlocks) {
  const uint64_t kBlockCount = 1 << 20;
  const uint32_t kBlockSize = 512;
  auto device = std::make_unique<FakeBlockDevice>(kBlockCount, kBlockSize);
  ASSERT_EQ(FormatFilesystem(device.get(), FilesystemOptions{}), ZX_OK);
  device->SetBlockCount(kBlockCount + 1);
  ASSERT_EQ(CheckMountability(std::move(device)), ZX_OK);
}

// Blobfs can be formatted on an FVM with a slice slice equal to two blocks.
TEST(FormatFilesystemTest, FormatFVMDeviceWithSmallestSliceSize) {
  const uint64_t kBlockCount = 1 << 20;
  const uint32_t kBlockSize = kBlobfsBlockSize;
  const uint64_t kSliceSize = kBlobfsBlockSize * 2;
  const uint64_t kSliceCount = 1028;
  auto device =
      std::make_unique<FakeFVMBlockDevice>(kBlockCount, kBlockSize, kSliceSize, kSliceCount);
  ASSERT_EQ(FormatFilesystem(device.get(), FilesystemOptions{}), ZX_OK);
  ASSERT_EQ(CheckMountability(std::move(device)), ZX_OK);
}

TEST(FormatFilesystemTest, FormatNonFVMDeviceDefaultInodeCount) {
  const uint64_t kBlockCount = MinimumFilesystemBlocks();
  const uint32_t kBlockSize = kBlobfsBlockSize;
  auto device = std::make_unique<FakeBlockDevice>(kBlockCount, kBlockSize);
  ASSERT_EQ(FormatFilesystem(device.get(), FilesystemOptions{}), ZX_OK);
  CheckDefaultInodeCount(std::move(device));
}

TEST(FormatFilesystemTest, FormatFvmDeviceDefaultJournalBlocks) {
  const uint64_t kBlockCount = 1 << 20;
  const uint32_t kBlockSize = kBlobfsBlockSize;
  const uint64_t kSliceSize = kBlobfsBlockSize * 2;
  const uint64_t kSliceCount = 1028;
  auto device =
      std::make_unique<FakeFVMBlockDevice>(kBlockCount, kBlockSize, kSliceSize, kSliceCount);
  ASSERT_EQ(FormatFilesystem(device.get(), FilesystemOptions{}), ZX_OK);
  CheckDefaultJournalBlocks(std::move(device));
}

TEST(FormatFilesystemTest, FormatNonFVMDeviceDefaultJournalBlocks) {
  const uint64_t kBlockCount = MinimumFilesystemBlocks();
  const uint32_t kBlockSize = kBlobfsBlockSize;
  auto device = std::make_unique<FakeBlockDevice>(kBlockCount, kBlockSize);
  ASSERT_EQ(FormatFilesystem(device.get(), FilesystemOptions{}), ZX_OK);
  CheckDefaultJournalBlocks(std::move(device));
}

TEST(FormatFilesystemTest, FormattedFilesystemHasSpecifiedOldestRevision) {
  const FilesystemOptions options{.oldest_minor_version = 1234u};
  const uint64_t kBlockCount = 1 << 20;
  const uint32_t kBlockSize = 512;
  auto device = std::make_unique<FakeBlockDevice>(kBlockCount, kBlockSize);
  ASSERT_EQ(FormatFilesystem(device.get(), options), ZX_OK);

  uint8_t block[kBlobfsBlockSize] = {};
  static_assert(sizeof(block) >= sizeof(Superblock));
  block_client::Reader reader(*device);
  ASSERT_EQ(reader.Read(0, kBlobfsBlockSize, &block), ZX_OK);
  Superblock* info = reinterpret_cast<Superblock*>(block);
  EXPECT_EQ(1234u, info->oldest_minor_version);
}

TEST(FormatFilesystemTest, FormattedFilesystemHasCurrentMinorVersionIfUnspecified) {
  const FilesystemOptions options;
  const uint64_t kBlockCount = 1 << 20;
  const uint32_t kBlockSize = 512;
  auto device = std::make_unique<FakeBlockDevice>(kBlockCount, kBlockSize);
  ASSERT_EQ(FormatFilesystem(device.get(), options), ZX_OK);

  uint8_t block[kBlobfsBlockSize] = {};
  static_assert(sizeof(block) >= sizeof(Superblock));
  block_client::Reader reader(*device);
  ASSERT_EQ(reader.Read(0, kBlobfsBlockSize, &block), ZX_OK);
  Superblock* info = reinterpret_cast<Superblock*>(block);
  EXPECT_EQ(kBlobfsCurrentMinorVersion, info->oldest_minor_version);
}

}  // namespace
}  // namespace blobfs
