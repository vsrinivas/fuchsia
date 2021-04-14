// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/errors.h>

#include <block-client/cpp/fake-device.h>
#include <gtest/gtest.h>

#include "src/storage/blobfs/blobfs.h"
#include "src/storage/blobfs/fsck.h"
#include "src/storage/blobfs/mkfs.h"
#include "src/storage/blobfs/test/blob_utils.h"
#include "src/storage/blobfs/test/blobfs_test_setup.h"

namespace blobfs {
namespace {

using ::block_client::BlockDevice;
using ::block_client::FakeBlockDevice;
using ::block_client::FakeFVMBlockDevice;

constexpr uint32_t kBlockSize = 512;
constexpr uint32_t kNumBlocks = 400 * kBlobfsBlockSize / kBlockSize;

std::unique_ptr<BlockDevice> CreateFakeBlockDevice(uint64_t num_blocks) {
  return std::make_unique<FakeBlockDevice>(num_blocks, kBlockSize);
}

std::unique_ptr<BlockDevice> CreateFakeFVMBlockDevice(uint64_t num_blocks) {
  return std::make_unique<FakeFVMBlockDevice>(num_blocks, kBlockSize, /*slice_size=*/32768,
                                              /*slice_capacity=*/500);
}

template <uint64_t oldest_minor_version,
          std::unique_ptr<BlockDevice> (*DeviceFactory)(uint64_t) = CreateFakeBlockDevice,
          uint64_t num_blocks = kNumBlocks>
class BlobfsTestAtMinorVersion : public BlobfsTestSetup, public testing::Test {
 public:
  void SetUp() final { srand(testing::UnitTest::GetInstance()->random_seed()); }

  std::unique_ptr<BlockDevice> CreateAndFormat() {
    FilesystemOptions options{.blob_layout_format = BlobLayoutFormat::kCompactMerkleTreeAtEnd,
                              .oldest_minor_version = oldest_minor_version};
    std::unique_ptr<BlockDevice> device = DeviceFactory(num_blocks);
    EXPECT_EQ(FormatFilesystem(device.get(), options), ZX_OK);
    return device;
  }

  MountOptions ReadOnlyOptions() const {
    return MountOptions{.writability = Writability::ReadOnlyDisk};
  }

  MountOptions ReadWriteOptions() const {
    return MountOptions{.writability = Writability::Writable};
  }
};

using BlobfsTestAtRev1 = BlobfsTestAtMinorVersion<kBlobfsMinorVersionBackupSuperblock - 1>;
using BlobfsTestAtRev1WithFvm =
    BlobfsTestAtMinorVersion<kBlobfsMinorVersionBackupSuperblock - 1, CreateFakeFVMBlockDevice>;
using BlobfsTestAtRev2 = BlobfsTestAtMinorVersion<kBlobfsMinorVersionBackupSuperblock>;
using BlobfsTestAtRev3 = BlobfsTestAtMinorVersion<kBlobfsMinorVersionNoOldCompressionFormats>;
using BlobfsTestAtRev4 =
    BlobfsTestAtMinorVersion<kBlobfsMinorVersionHostToolHandlesNullBlobCorrectly>;
using BlobfsTestAtFutureRev = BlobfsTestAtMinorVersion<kBlobfsCurrentMinorVersion + 1>;

TEST_F(BlobfsTestAtRev1, UpgradedToLaterMinorVersion) {
  Mount(CreateAndFormat(), ReadWriteOptions());
  auto device = Unmount();

  // Read the superblock, verify the oldest revision is set to the current revision
  // This involves three migration steps:
  //  - 1->2 (NOP, since FVM is disabled so there's no backup superblock)
  //  - 2->3 (Overwrite blobs in old compression formats)
  //  - 3->4 (Fix zero-length extent in the null blob)
  uint8_t block[kBlobfsBlockSize] = {};
  static_assert(sizeof(block) >= sizeof(Superblock));
  ASSERT_EQ(device->ReadBlock(0, kBlobfsBlockSize, &block), ZX_OK);
  Superblock* info = reinterpret_cast<Superblock*>(block);
  EXPECT_EQ(info->oldest_minor_version, kBlobfsMinorVersionHostToolHandlesNullBlobCorrectly);

  ASSERT_EQ(Fsck(std::move(device), ReadOnlyOptions()), ZX_OK);
}

TEST_F(BlobfsTestAtRev1WithFvm, OldInstanceTriggersWriteToBackupSuperblock) {
  Mount(CreateAndFormat(), ReadWriteOptions());
  ASSERT_TRUE(blobfs()->Info().flags & kBlobFlagFVM);
  auto device = Unmount();

  // Read the superblock, verify the oldest revision is set to the current revision
  // This involves three migration steps:
  //  - 1->2 (Add backup superblock)
  //  - 2->3 (Overwrite blobs in old compression formats)
  //  - 3->4 (Fix zero-length extent in the null blob)
  uint8_t block[kBlobfsBlockSize] = {};
  static_assert(sizeof(block) >= sizeof(Superblock));
  ASSERT_EQ(device->ReadBlock(0, kBlobfsBlockSize, &block), ZX_OK);
  Superblock* info = reinterpret_cast<Superblock*>(block);
  EXPECT_EQ(info->oldest_minor_version, kBlobfsMinorVersionHostToolHandlesNullBlobCorrectly);

  ASSERT_EQ(Fsck(std::move(device), ReadOnlyOptions()), ZX_OK);
}

TEST_F(BlobfsTestAtRev2, UpgradedToLaterMinorVersion) {
  Mount(CreateAndFormat(), ReadWriteOptions());
  auto device = Unmount();

  // Read the superblock, verify the oldest revision is set to the current revision
  // This involves two migration steps:
  //  - 2->3 (Overwrite blobs in old compression formats)
  //  - 3->4 (Fix zero-length extent in the null blob)
  uint8_t block[kBlobfsBlockSize] = {};
  static_assert(sizeof(block) >= sizeof(Superblock));
  ASSERT_EQ(device->ReadBlock(0, kBlobfsBlockSize, &block), ZX_OK);
  Superblock* info = reinterpret_cast<Superblock*>(block);
  EXPECT_EQ(info->oldest_minor_version, kBlobfsMinorVersionHostToolHandlesNullBlobCorrectly);

  ASSERT_EQ(Fsck(std::move(device), ReadOnlyOptions()), ZX_OK);
}

TEST_F(BlobfsTestAtRev3, UpgradedToLaterMinorVersion) {
  Mount(CreateAndFormat(), ReadWriteOptions());
  auto device = Unmount();

  // Read the superblock, verify the oldest revision is set to the current revision
  // This involves two migration steps:
  //  - 3->4 (Fix zero-length extent in the null blob)
  uint8_t block[kBlobfsBlockSize] = {};
  static_assert(sizeof(block) >= sizeof(Superblock));
  ASSERT_EQ(device->ReadBlock(0, kBlobfsBlockSize, &block), ZX_OK);
  Superblock* info = reinterpret_cast<Superblock*>(block);
  EXPECT_EQ(info->oldest_minor_version, kBlobfsMinorVersionHostToolHandlesNullBlobCorrectly);

  ASSERT_EQ(Fsck(std::move(device), ReadOnlyOptions()), ZX_OK);
}

TEST_F(BlobfsTestAtRev4, NotUpgraded) {
  Mount(CreateAndFormat(), ReadWriteOptions());
  auto device = Unmount();

  // Read the superblock, verify the oldest revision is unmodified
  uint8_t block[kBlobfsBlockSize] = {};
  static_assert(sizeof(block) >= sizeof(Superblock));
  ASSERT_EQ(device->ReadBlock(0, kBlobfsBlockSize, &block), ZX_OK);
  Superblock* info = reinterpret_cast<Superblock*>(block);
  EXPECT_EQ(info->oldest_minor_version, kBlobfsMinorVersionHostToolHandlesNullBlobCorrectly);

  ASSERT_EQ(Fsck(std::move(device), ReadOnlyOptions()), ZX_OK);
}

TEST_F(BlobfsTestAtFutureRev, OldestMinorVersionSetToDriverMinorVersion) {
  Mount(CreateAndFormat(), ReadWriteOptions());
  auto device = Unmount();

  // Read the superblock, verify the oldest revision is set to the current revision
  uint8_t block[kBlobfsBlockSize] = {};
  static_assert(sizeof(block) >= sizeof(Superblock));
  ASSERT_EQ(device->ReadBlock(0, kBlobfsBlockSize, &block), ZX_OK);
  Superblock* info = reinterpret_cast<Superblock*>(block);
  EXPECT_EQ(info->oldest_minor_version, kBlobfsCurrentMinorVersion);

  ASSERT_EQ(Fsck(std::move(device), ReadOnlyOptions()), ZX_OK);
}

}  // namespace
}  // namespace blobfs
