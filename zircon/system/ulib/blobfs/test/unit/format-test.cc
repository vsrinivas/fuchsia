// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <blobfs/format.h>
#include <blobfs/mkfs.h>
#include <block-client/cpp/fake-device.h>
#include <zxtest/zxtest.h>

#include "blobfs.h"

namespace blobfs {
namespace {

using block_client::FakeBlockDevice;
using block_client::FakeFVMBlockDevice;

zx_status_t CheckMountability(std::unique_ptr<BlockDevice> device) {
  MountOptions options = {};
  options.writability = Writability::ReadOnlyFilesystem;
  options.metrics = false;
  options.journal = true;
  std::unique_ptr<Blobfs> blobfs = nullptr;
  return Blobfs::Create(nullptr, std::move(device), &options, zx::resource(), &blobfs);
}

void CheckDefaultInodeCount(std::unique_ptr<BlockDevice> device) {
  MountOptions options = {};
  std::unique_ptr<Blobfs> blobfs;
  ASSERT_OK(Blobfs::Create(nullptr, std::move(device), &options, zx::resource(), &blobfs));
  ASSERT_GE(blobfs->Info().inode_count, kBlobfsDefaultInodeCount);
}

void CheckDefaultJournalBlocks(std::unique_ptr<BlockDevice> device) {
  MountOptions options = {};
  std::unique_ptr<Blobfs> blobfs;
  ASSERT_OK(Blobfs::Create(nullptr, std::move(device), &options, zx::resource(), &blobfs));
  ASSERT_GE(blobfs->Info().journal_block_count, kDefaultJournalBlocks);
}

// Formatting filesystems should fail on devices that cannot be written.
TEST(FormatFilesystemTest, CannotFormatReadOnlyDevice) {
  auto device = std::make_unique<FakeBlockDevice>(1 << 20, 512);
  device->SetInfoFlags(fuchsia_hardware_block_FLAG_READONLY);
  ASSERT_EQ(ZX_ERR_ACCESS_DENIED, FormatFilesystem(device.get()));
}

// Formatting filesystems should fail on devices that don't contain any blocks.
TEST(FormatFilesystemTest, CannotFormatEmptyDevice) {
  auto device = std::make_unique<FakeBlockDevice>(0, 0);
  ASSERT_EQ(ZX_ERR_NO_SPACE, FormatFilesystem(device.get()));
}

// Formatting filesystems should fail on devices that aren't empty, but are
// still too small to contain a filesystem.
TEST(FormatFilesystemTest, CannotFormatSmallDevice) {
  auto device = std::make_unique<FakeBlockDevice>(1, 512);
  ASSERT_EQ(ZX_ERR_NO_SPACE, FormatFilesystem(device.get()));
}

// Formatting filesystems should fail on devices which have a block size that
// does not cleanly divide the blobfs block size.
TEST(FormatFilesystemTest, CannotFormatDeviceWithNonDivisorBlockSize) {
  const uint64_t kBlockCount = 1 << 20;
  uint64_t kBlockSize = 511;
  EXPECT_NE(kBlobfsBlockSize % kBlockSize, 0, "Expected non-divisor block size");
  auto device = std::make_unique<FakeBlockDevice>(kBlockCount, kBlockSize);
  ASSERT_EQ(ZX_ERR_IO_INVALID, FormatFilesystem(device.get()));
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
    ASSERT_OK(FormatFilesystem(device.get()));
    ASSERT_OK(CheckMountability(std::move(device)));
  }

  // One block smaller than the smallest possible device.
  {
    auto device = std::make_unique<FakeBlockDevice>(kBlockCount - 1, kBlockSize);
    ASSERT_EQ(ZX_ERR_NO_SPACE, FormatFilesystem(device.get()));
  }
}

// Calculates the smallest number of blobfs slices to generate a valid blobfs format.
uint64_t MinimumFilesystemSlices(uint64_t kSliceSize) {
  uint64_t kBlocksPerSlice = kSliceSize / kBlobfsBlockSize;
  auto BlocksToSlices = [kBlocksPerSlice](uint64_t blocks) {
    return fbl::round_up(blocks, kBlocksPerSlice) / kBlocksPerSlice;
  };

  const uint64_t kSuperBlockSlices = BlocksToSlices(1);
  const uint64_t kInodeSlices = 1;
  const uint64_t kJournalSlices = BlocksToSlices(kDefaultJournalBlocks);
  const uint64_t kDataSlices = BlocksToSlices(kMinimumDataBlocks);
  const uint64_t kBlockMapSlices = BlocksToSlices(BlocksRequiredForBits(kMinimumDataBlocks));

  return kSuperBlockSlices + kInodeSlices + kJournalSlices + kDataSlices + kBlockMapSlices;
}

TEST(FormatFilesystemTest, FormatFVMSmallestDevice) {
  const uint32_t kBlockSize = 512;
  const uint64_t kSliceSize = kBlobfsBlockSize * 8;
  const uint64_t kSliceCount = MinimumFilesystemSlices(kSliceSize);
  const uint64_t kDiskBlockRatio = kBlobfsBlockSize / kBlockSize;
  const uint64_t kBlockCount = kDiskBlockRatio * MinimumFilesystemBlocks();

  // Smallest possible device.
  {
    auto device =
        std::make_unique<FakeFVMBlockDevice>(kBlockCount, kBlockSize, kSliceSize, kSliceCount);
    ASSERT_OK(FormatFilesystem(device.get()));
    ASSERT_OK(CheckMountability(std::move(device)));
  }

  // One slice smaller than the smallest possible device.
  {
    auto device =
        std::make_unique<FakeFVMBlockDevice>(kBlockCount, kBlockSize, kSliceSize, kSliceCount - 1);
    ASSERT_EQ(ZX_ERR_NO_SPACE, FormatFilesystem(device.get()));
  }
}

// Blobfs can be formatted on slightly larger devices as well.
TEST(FormatFilesystemTest, FormatNonFVMDevice) {
  const uint64_t kBlockCount = 1 << 20;
  const uint32_t kBlockSize = 512;
  auto device = std::make_unique<FakeBlockDevice>(kBlockCount, kBlockSize);
  ASSERT_OK(FormatFilesystem(device.get()));
  ASSERT_OK(CheckMountability(std::move(device)));
}

TEST(FormatFilesystemTest, FormatFVMDevice) {
  const uint64_t kBlockCount = 1 << 20;
  const uint32_t kBlockSize = 512;
  const uint64_t kSliceSize = kBlobfsBlockSize * 8;
  const uint64_t kSliceCount = 1028;
  auto device =
      std::make_unique<FakeFVMBlockDevice>(kBlockCount, kBlockSize, kSliceSize, kSliceCount);
  ASSERT_OK(FormatFilesystem(device.get()));
  ASSERT_OK(CheckMountability(std::move(device)));
}

// Blobfs can be formatted on devices that have "trailing device block(s)" that
// cannot be fully addressed by blobfs blocks.
TEST(FormatFilesystemTest, FormatNonFVMDeviceWithTrailingDiskBlock) {
  const uint64_t kBlockCount = (1 << 20) + 1;
  const uint32_t kBlockSize = 512;
  auto device = std::make_unique<FakeBlockDevice>(kBlockCount, kBlockSize);
  ASSERT_OK(FormatFilesystem(device.get()));
  ASSERT_OK(CheckMountability(std::move(device)));
}

TEST(FormatFilesystemTest, FormatFVMDeviceWithTrailingDiskBlock) {
  const uint64_t kBlockCount = (1 << 20) + 1;
  const uint32_t kBlockSize = 512;
  const uint64_t kSliceSize = kBlobfsBlockSize * 8;
  const uint64_t kSliceCount = 1028;
  auto device =
      std::make_unique<FakeFVMBlockDevice>(kBlockCount, kBlockSize, kSliceSize, kSliceCount);
  ASSERT_OK(FormatFilesystem(device.get()));
  ASSERT_OK(CheckMountability(std::move(device)));
}

// Blobfs can be formatted on devices that have block sizes up to and including
// the blobfs block size itself.
TEST(FormatFilesystemTest, FormatNonFVMDeviceWithLargestBlockSize) {
  const uint64_t kBlockCount = 1 << 20;
  const uint32_t kBlockSize = kBlobfsBlockSize;
  auto device = std::make_unique<FakeBlockDevice>(kBlockCount, kBlockSize);
  ASSERT_OK(FormatFilesystem(device.get()));
  ASSERT_OK(CheckMountability(std::move(device)));
}

TEST(FormatFilesystemTest, FormatFVMDeviceWithLargestBlockSize) {
  const uint64_t kBlockCount = 1 << 20;
  const uint32_t kBlockSize = kBlobfsBlockSize;
  const uint64_t kSliceSize = kBlobfsBlockSize * 8;
  const uint64_t kSliceCount = 1028;
  auto device =
      std::make_unique<FakeFVMBlockDevice>(kBlockCount, kBlockSize, kSliceSize, kSliceCount);
  ASSERT_OK(FormatFilesystem(device.get()));
  ASSERT_OK(CheckMountability(std::move(device)));
}

// Blobfs cannot be formatted on devices that have block sizes larger than the
// blobfs block size itself.
TEST(FormatFilesystemTest, FormatNonFVMDeviceWithTooLargeBlockSize) {
  const uint64_t kBlockCount = 1 << 20;
  const uint32_t kBlockSize = kBlobfsBlockSize * 2;
  auto device = std::make_unique<FakeBlockDevice>(kBlockCount, kBlockSize);
  ASSERT_EQ(ZX_ERR_IO_INVALID, FormatFilesystem(device.get()));
  ASSERT_EQ(ZX_ERR_IO, CheckMountability(std::move(device)));
}

TEST(FormatFilesystemTest, FormatFVMDeviceWithTooLargeBlockSize) {
  const uint64_t kBlockCount = 1 << 20;
  const uint32_t kBlockSize = kBlobfsBlockSize * 2;
  const uint64_t kSliceSize = kBlobfsBlockSize * 8;
  const uint64_t kSliceCount = 1028;
  auto device =
      std::make_unique<FakeFVMBlockDevice>(kBlockCount, kBlockSize, kSliceSize, kSliceCount);
  ASSERT_EQ(ZX_ERR_IO_INVALID, FormatFilesystem(device.get()));
  ASSERT_EQ(ZX_ERR_IO, CheckMountability(std::move(device)));
}

// Validates that a formatted filesystem, mounted as writable, is converted
// to read-only on a device that is not writable.
TEST(FormatFilesystemTest, FormatDeviceNoJournalAutoConvertReadonly) {
  const uint64_t kBlockCount = 1 << 20;
  const uint32_t kBlockSize = kBlobfsBlockSize;
  auto device = std::make_unique<FakeBlockDevice>(kBlockCount, kBlockSize);
  ASSERT_OK(FormatFilesystem(device.get()));
  device->SetInfoFlags(fuchsia_hardware_block_FLAG_READONLY);

  MountOptions mount_options = {};
  mount_options.writability = Writability::Writable;
  mount_options.metrics = false;
  mount_options.journal = false;
  std::unique_ptr<Blobfs> fs = nullptr;
  ASSERT_OK(Blobfs::Create(nullptr, std::move(device), &mount_options, zx::resource(), &fs));
  ASSERT_EQ(Writability::ReadOnlyDisk, fs->writability());
}

// Validates that a formatted filesystem mounted as writable with a journal cannot be mounted on a
// read-only device. This "auto-conversion" is disabled because journal replay is necessary
// to guarantee filesystem correctness, which involves writeback.
TEST(FormatFilesystemTest, FormatDeviceWithJournalCannotAutoConvertReadonly) {
  const uint64_t kBlockCount = 1 << 20;
  const uint32_t kBlockSize = kBlobfsBlockSize;
  auto device = std::make_unique<FakeBlockDevice>(kBlockCount, kBlockSize);
  ASSERT_OK(FormatFilesystem(device.get()));
  device->SetInfoFlags(fuchsia_hardware_block_FLAG_READONLY);

  MountOptions options = {};
  options.writability = Writability::Writable;
  options.metrics = false;
  options.journal = true;
  std::unique_ptr<Blobfs> blobfs = nullptr;
  ASSERT_EQ(ZX_ERR_ACCESS_DENIED,
            Blobfs::Create(nullptr, std::move(device), &options, zx::resource(), &blobfs));
}

// After formatting a filesystem with block size valid block size N, mounting on
// a device with an invalid block size should fail.
TEST(FormatFilesystemTest, CreateBlobfsFailureOnUnalignedBlockSize) {
  const uint64_t kBlockCount = 1 << 20;
  const uint32_t kBlockSize = 512;
  auto device = std::make_unique<FakeBlockDevice>(kBlockCount, kBlockSize);
  ASSERT_OK(FormatFilesystem(device.get()));
  device->SetBlockSize(kBlockSize + 1);
  ASSERT_EQ(ZX_ERR_IO, CheckMountability(std::move(device)));
}

// After formatting a filesystem with block size valid block count N, mounting
// on a device less M blocks (for M < N) should fail.
TEST(FormatFilesystemTest, CreateBlobfsFailureWithLessBlocks) {
  const uint64_t kBlockCount = 1 << 20;
  const uint32_t kBlockSize = 512;
  auto device = std::make_unique<FakeBlockDevice>(kBlockCount, kBlockSize);
  ASSERT_OK(FormatFilesystem(device.get()));
  device->SetBlockCount(kBlockCount - 1);
  ASSERT_EQ(ZX_ERR_BAD_STATE, CheckMountability(std::move(device)));
}

// After formatting a filesystem with block size valid block count N, mounting
// on a device less M blocks (for M > N) should succeed.
TEST(FormatFilesystemTest, CreateBlobfsSuccessWithMoreBlocks) {
  const uint64_t kBlockCount = 1 << 20;
  const uint32_t kBlockSize = 512;
  auto device = std::make_unique<FakeBlockDevice>(kBlockCount, kBlockSize);
  ASSERT_OK(FormatFilesystem(device.get()));
  device->SetBlockCount(kBlockCount + 1);
  ASSERT_OK(CheckMountability(std::move(device)));
}

// Blobfs cannot be formatted on an FVM with a slice size smaller than a block size.
TEST(FormatFilesystemTest, FormatFVMDeviceWithTooSmallSliceSize) {
  const uint64_t kBlockCount = 1 << 20;
  const uint32_t kBlockSize = kBlobfsBlockSize;
  const uint64_t kSliceSize = kBlobfsBlockSize / 2;
  const uint64_t kSliceCount = 1028;
  auto device =
      std::make_unique<FakeFVMBlockDevice>(kBlockCount, kBlockSize, kSliceSize, kSliceCount);
  ASSERT_EQ(ZX_ERR_IO_INVALID, FormatFilesystem(device.get()));
  ASSERT_EQ(ZX_ERR_INVALID_ARGS, CheckMountability(std::move(device)));
}

// Blobfs can be formatted on an FVM with a slice slice equal to a block size.
TEST(FormatFilesystemTest, FormatFVMDeviceWithSmallestSliceSize) {
  const uint64_t kBlockCount = 1 << 20;
  const uint32_t kBlockSize = kBlobfsBlockSize;
  const uint64_t kSliceSize = kBlobfsBlockSize;
  const uint64_t kSliceCount = 1028;
  auto device =
      std::make_unique<FakeFVMBlockDevice>(kBlockCount, kBlockSize, kSliceSize, kSliceCount);
  ASSERT_OK(FormatFilesystem(device.get()));
  ASSERT_OK(CheckMountability(std::move(device)));
}

// Blobfs cannot be formatted on an FVM with a slice size that does not divide
// the blobfs block size.
TEST(FormatFilesystemTest, FormatFVMDeviceWithNonDivisibleSliceSize) {
  const uint64_t kBlockCount = 1 << 20;
  const uint32_t kBlockSize = kBlobfsBlockSize;
  const uint64_t kSliceSize = kBlobfsBlockSize * 8 + 1;
  const uint64_t kSliceCount = 1028;
  auto device =
      std::make_unique<FakeFVMBlockDevice>(kBlockCount, kBlockSize, kSliceSize, kSliceCount);
  ASSERT_EQ(ZX_ERR_IO_INVALID, FormatFilesystem(device.get()));
  ASSERT_EQ(ZX_ERR_INVALID_ARGS, CheckMountability(std::move(device)));
}

TEST(FormatFilesystemTest, FormatNonFVMDeviceDefaultInodeCount) {
  const uint64_t kBlockCount = MinimumFilesystemBlocks();
  const uint32_t kBlockSize = kBlobfsBlockSize;
  auto device = std::make_unique<FakeBlockDevice>(kBlockCount, kBlockSize);
  ASSERT_OK(FormatFilesystem(device.get()));
  CheckDefaultInodeCount(std::move(device));
}

TEST(FormatFilesystemTest, FormatFvmDeviceDefaultJournalBlocks) {
  const uint64_t kBlockCount = MinimumFilesystemBlocks();
  const uint32_t kBlockSize = kBlobfsBlockSize;
  const uint64_t kSliceSize = kBlobfsBlockSize;
  const uint64_t kSliceCount = 1028;
  auto device =
      std::make_unique<FakeFVMBlockDevice>(kBlockCount, kBlockSize, kSliceSize, kSliceCount);
  ASSERT_OK(FormatFilesystem(device.get()));
  CheckDefaultJournalBlocks(std::move(device));
}

TEST(FormatFilesystemTest, FormatNonFVMDeviceDefaultJournalBlocks) {
  const uint64_t kBlockCount = MinimumFilesystemBlocks();
  const uint32_t kBlockSize = kBlobfsBlockSize;
  auto device = std::make_unique<FakeBlockDevice>(kBlockCount, kBlockSize);
  ASSERT_OK(FormatFilesystem(device.get()));
  CheckDefaultJournalBlocks(std::move(device));
}

}  // namespace
}  // namespace blobfs
