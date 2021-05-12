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

using BlobfsTestAtRev2 = BlobfsTestAtMinorVersion<kBlobfsMinorVersionBackupSuperblock>;
using BlobfsTestAtRev4 =
    BlobfsTestAtMinorVersion<kBlobfsMinorVersionHostToolHandlesNullBlobCorrectly>;
using BlobfsTestAtFutureRev = BlobfsTestAtMinorVersion<kBlobfsCurrentMinorVersion + 1>;

// Writing v2 isn't supported, formatting should fail.
TEST_F(BlobfsTestAtRev2, WontFormat) {
  EXPECT_EQ(ZX_ERR_NOT_SUPPORTED, Mount(CreateAndFormat(), ReadWriteOptions()));
}

// Tests that revision 2 won't load. This is a "rev 4" test because we can't actually write
// revision 2 formats. To bypass this, the test creates a rev 4 image and manually updates the
// superblock to rev 2.
TEST_F(BlobfsTestAtRev4, WontReadRev2) {
  Mount(CreateAndFormat(), ReadWriteOptions());
  auto device = Unmount();

  // Read the superblock block.
  {
    // Scope the vmo buffer. Destroying implicitly on test exit seems to cause ordering issues.
    storage::VmoBuffer buffer;
    ASSERT_EQ(buffer.Initialize(device.get(), 1, kBlobfsBlockSize, "test_buffer"), ZX_OK);
    block_fifo_request_t request{
        .opcode = BLOCKIO_READ,
        .vmoid = buffer.vmoid(),
        .length = kBlobfsBlockSize / kBlockSize,
        .vmo_offset = 0,
        .dev_offset = 0,
    };
    ASSERT_EQ(device->FifoTransaction(&request, 1), ZX_OK);

    // Downgrade to revision 2.
    Superblock* info = reinterpret_cast<Superblock*>(buffer.Data(0));
    info->oldest_minor_version = kBlobfsMinorVersionBackupSuperblock;

    request.opcode = BLOCKIO_WRITE;
    ASSERT_EQ(device->FifoTransaction(&request, 1), ZX_OK);
  }

  // The device should now fail to mount.
  ASSERT_EQ(ZX_ERR_NOT_SUPPORTED, Mount(std::move(device)));
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
