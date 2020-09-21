// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Tests minfs backup superblock behavior.

#include <lib/cksum.h>
#include <unistd.h>

#include <block-client/cpp/fake-device.h>
#include <minfs/fsck.h>
#include <minfs/superblock.h>
#include <zxtest/zxtest.h>

#include "minfs/format.h"
#include "minfs_private.h"

namespace minfs {
namespace {

using block_client::FakeBlockDevice;
constexpr size_t abm_block = 5;
constexpr size_t ibm_block = 6;
constexpr size_t data_block = 7;
constexpr size_t integrity_block = 8;

// Mock TransactionHandler class to be used in superblock tests.
class MockTransactionHandler : public fs::DeviceTransactionHandler {
 public:
  MockTransactionHandler(block_client::BlockDevice* device) { device_ = device; }

  MockTransactionHandler(const MockTransactionHandler&) = delete;
  MockTransactionHandler(MockTransactionHandler&&) = delete;
  MockTransactionHandler& operator=(const MockTransactionHandler&) = delete;
  MockTransactionHandler& operator=(MockTransactionHandler&&) = delete;

  // fs::TransactionHandler Interface.
  uint64_t BlockNumberToDevice(uint64_t block_num) const final { return block_num; }

  block_client::BlockDevice* GetDevice() final { return device_; }

 private:
  block_client::BlockDevice* device_;
};

void CreateAndRegisterVmo(block_client::BlockDevice* device, size_t blocks, zx::vmo* vmo,
                          storage::OwnedVmoid* vmoid) {
  fuchsia_hardware_block_BlockInfo info = {};
  ASSERT_OK(device->BlockGetInfo(&info));
  ASSERT_OK(zx::vmo::create(blocks * info.block_size, 0, vmo));
  ASSERT_OK(device->BlockAttachVmo(*vmo, &vmoid->GetReference(device)));
}

void FillSuperblockFields(Superblock* info) {
  constexpr uint32_t kDefaultAllocCount = 2;
  info->magic0 = kMinfsMagic0;
  info->magic1 = kMinfsMagic1;
  info->version_major = kMinfsMajorVersion;
  info->version_minor = kMinfsMinorVersion;
  info->flags = kMinfsFlagClean;
  info->block_size = kMinfsBlockSize;
  info->inode_size = kMinfsInodeSize;
  info->dat_block = data_block;
  info->integrity_start_block = integrity_block;
  info->ibm_block = ibm_block;
  info->abm_block = abm_block;
  info->ino_block = abm_block;
  info->block_count = 1;
  info->inode_count = 1;
  info->alloc_block_count = kDefaultAllocCount;
  info->alloc_inode_count = kDefaultAllocCount;
  info->generation_count = 0;
  minfs::UpdateChecksum(info);
}

// Fills write request for the respective block locations.
void FillWriteRequest(MockTransactionHandler* transaction_handler, uint32_t first_block_location,
                      uint32_t second_block_location, vmoid_t vmoid,
                      block_fifo_request_t* out_requests) {
  out_requests[0].opcode = BLOCKIO_WRITE;
  out_requests[0].vmoid = vmoid;
  out_requests[0].length = 1;
  out_requests[0].vmo_offset = 0;
  out_requests[0].dev_offset = first_block_location;
  out_requests[1].opcode = BLOCKIO_WRITE;
  out_requests[1].vmoid = vmoid;
  out_requests[1].length = 1;
  out_requests[1].vmo_offset = 1;
  out_requests[1].dev_offset = second_block_location;
}

// Tests the alloc_*_counts bitmap reconstruction.
TEST(SuperblockTest, TestBitmapReconstruction) {
  Superblock info = {};
  FillSuperblockFields(&info);

  FakeBlockDevice device = FakeBlockDevice(100, kMinfsBlockSize);
  auto transaction_handler =
      std::unique_ptr<MockTransactionHandler>(new MockTransactionHandler(&device));

  uint8_t block[minfs::kMinfsBlockSize];
  memset(block, 0, sizeof(block));

  // Fill up the entire bitmap sparsely with random 1 and 0.
  // 0xFF = 8 bits set.
  block[0] = 0xFF;
  block[30] = 0xFF;
  block[100] = 0xFF;
  block[5000] = 0xFF;

  zx::vmo vmo;
  block_fifo_request_t request[2];
  storage::OwnedVmoid vmoid;
  ASSERT_NO_FAILURES(CreateAndRegisterVmo(&device, 2, &vmo, &vmoid));
  ASSERT_OK(vmo.write(block, 0, kMinfsBlockSize));
  ASSERT_OK(vmo.write(block, kMinfsBlockSize, kMinfsBlockSize));

  // Write abm_block and ibm_block to disk.
  FillWriteRequest(transaction_handler.get(), abm_block, ibm_block, vmoid.get(), request);

  ASSERT_OK(device.FifoTransaction(request, 2));

  // Reconstruct alloc_*_counts from respective bitmaps.
  zx_status_t status = ReconstructAllocCounts(transaction_handler.get(), &device, &info);
  ASSERT_EQ(status, ZX_OK);

  // Confirm that alloc_*_counts are updated correctly.
  ASSERT_EQ(32, info.alloc_block_count);
  ASSERT_EQ(32, info.alloc_inode_count);

  // Write all bits unset for abm_block and ibm_block.
  memset(block, 0, sizeof(block));

  // Write the bitmaps to disk.
  ASSERT_OK(vmo.write(block, 0, sizeof(block)));
  ASSERT_OK(vmo.write(block, sizeof(block), sizeof(block)));
  ASSERT_OK(device.FifoTransaction(request, 2));

  // Reconstruct alloc_*_counts from respective bitmaps.
  status = ReconstructAllocCounts(transaction_handler.get(), &device, &info);
  ASSERT_EQ(status, ZX_OK);

  // Confirm the alloc_*_counts are updated correctly.
  ASSERT_EQ(0, info.alloc_block_count);
  ASSERT_EQ(0, info.alloc_inode_count);

  memset(block, 0, sizeof(block));

  // Fill up the entire bitmap sparsely with random 1 and 0.
  block[0] = 0x88;
  block[30] = 0xAA;
  block[100] = 0x44;
  block[5000] = 0x2C;

  // Write the bitmaps on disk.
  ASSERT_OK(vmo.write(block, 0, sizeof(block)));
  ASSERT_OK(vmo.write(block, sizeof(block), sizeof(block)));
  ASSERT_OK(device.FifoTransaction(request, 2));

  // Reconstruct alloc_*_counts from respective bitmaps.
  status = ReconstructAllocCounts(transaction_handler.get(), &device, &info);
  ASSERT_EQ(status, ZX_OK);

  // Confirm the alloc_*_counts are updated correctly.
  ASSERT_EQ(11, info.alloc_block_count);
  ASSERT_EQ(11, info.alloc_inode_count);
}

// Tests corrupt superblock and corrupt backup superblock.
TEST(SuperblockTest, TestCorruptSuperblockWithoutCorrection) {
  Superblock info = {};
  FillSuperblockFields(&info);

  FakeBlockDevice device = FakeBlockDevice(100, kMinfsBlockSize);
  auto transaction_handler =
      std::unique_ptr<MockTransactionHandler>(new MockTransactionHandler(&device));

  Superblock backup;
  memcpy(&backup, &info, sizeof(backup));

  // Corrupt original Superblock.
  info.version_major = 0xdeadbeef;
  info.version_minor = 0xdeadbeef;

  // Corrupt backup Superblock.
  backup.version_major = 0x55;
  backup.version_minor = 0x55;

  // Write superblock and backup to disk.
  block_fifo_request_t request[2];
  zx::vmo vmo;
  storage::OwnedVmoid vmoid;
  ASSERT_NO_FAILURES(CreateAndRegisterVmo(&device, 2, &vmo, &vmoid));
  ASSERT_OK(vmo.write(&info, 0, kMinfsBlockSize));
  ASSERT_OK(vmo.write(&backup, kMinfsBlockSize, kMinfsBlockSize));

  FillWriteRequest(transaction_handler.get(), kSuperblockStart, kNonFvmSuperblockBackup,
                   vmoid.get(), request);
  ASSERT_OK(device.FifoTransaction(request, 2));

  // Try to correct the corrupted superblock.
  zx_status_t status = RepairSuperblock(transaction_handler.get(), &device,
                                        info.dat_block + info.block_count, &info);
  ASSERT_NE(status, ZX_OK);
  // Read back the superblock and backup superblock.
  ASSERT_OK(device.ReadBlock(kSuperblockStart, kMinfsBlockSize, &info));
  ASSERT_OK(device.ReadBlock(kNonFvmSuperblockBackup, kMinfsBlockSize, &backup));

  // Confirm that the superblock is not updated by backup.
  ASSERT_BYTES_NE(&info, &backup, sizeof(backup));
  ASSERT_EQ(0xdeadbeef, info.version_major);
  ASSERT_EQ(0xdeadbeef, info.version_minor);
  ASSERT_EQ(0x55, backup.version_major);
  ASSERT_EQ(0x55, backup.version_minor);
}

// Tests corrupt superblock and non-corrupt backup superblock.
TEST(SuperblockTest, TestCorruptSuperblockWithCorrection) {
  Superblock info = {};
  FillSuperblockFields(&info);

  FakeBlockDevice device = FakeBlockDevice(100, kMinfsBlockSize);
  auto transaction_handler =
      std::unique_ptr<MockTransactionHandler>(new MockTransactionHandler(&device));

  Superblock backup;
  memcpy(&backup, &info, sizeof(backup));

  // Corrupt original Superblock.
  info.version_major = 0xdeadbeef;
  info.version_minor = 0xdeadbeef;

  // Write superblock and backup to disk.
  block_fifo_request_t request[2];
  zx::vmo vmo;
  storage::OwnedVmoid vmoid;
  ASSERT_NO_FAILURES(CreateAndRegisterVmo(&device, 2, &vmo, &vmoid));
  ASSERT_OK(vmo.write(&info, 0, kMinfsBlockSize));
  ASSERT_OK(vmo.write(&backup, kMinfsBlockSize, kMinfsBlockSize));
  FillWriteRequest(transaction_handler.get(), kSuperblockStart, kNonFvmSuperblockBackup,
                   vmoid.get(), request);
  ASSERT_OK(device.FifoTransaction(request, 2));
  // Try to correct the corrupted superblock.
  zx_status_t status = RepairSuperblock(transaction_handler.get(), &device,
                                        info.dat_block + info.block_count, &info);
  ASSERT_EQ(status, ZX_OK);

  // Read back the superblock and backup superblock.
  ASSERT_OK(device.ReadBlock(kSuperblockStart, kMinfsBlockSize, &info));
  ASSERT_OK(device.ReadBlock(kNonFvmSuperblockBackup, kMinfsBlockSize, &backup));

  // Confirm that the superblock is updated by backup.
  ASSERT_BYTES_EQ(&info, &backup, sizeof(backup));
}

// Tests if Repair of corrupted superblock reconstructs the bitmaps correctly.
TEST(SuperblockTest, TestRepairSuperblockWithBitmapReconstruction) {
  FakeBlockDevice device = FakeBlockDevice(100, kMinfsBlockSize);
  auto transaction_handler =
      std::unique_ptr<MockTransactionHandler>(new MockTransactionHandler(&device));

  Superblock backup;
  FillSuperblockFields(&backup);
  backup.alloc_block_count = 0;
  backup.alloc_inode_count = 0;
  minfs::UpdateChecksum(&backup);

  Superblock info = {};
  // Write corrupted superblock and backup to disk.
  block_fifo_request_t request[2];
  zx::vmo vmo;
  storage::OwnedVmoid vmoid;
  ASSERT_NO_FAILURES(CreateAndRegisterVmo(&device, 2, &vmo, &vmoid));
  ASSERT_OK(vmo.write(&info, 0, kMinfsBlockSize));
  ASSERT_OK(vmo.write(&backup, kMinfsBlockSize, kMinfsBlockSize));
  FillWriteRequest(transaction_handler.get(), kSuperblockStart, kNonFvmSuperblockBackup,
                   vmoid.get(), request);
  ASSERT_OK(device.FifoTransaction(request, 2));

  uint8_t block[minfs::kMinfsBlockSize];
  memset(block, 0, sizeof(block));

  // Fill up the entire bitmap sparsely with random 1 and 0.
  block[0] = 0xFF;
  block[30] = 0xFF;
  block[100] = 0xFF;
  block[5000] = 0xFF;

  // Write abm_block and ibm_block to disk.
  ASSERT_OK(vmo.write(block, 0, kMinfsBlockSize));
  ASSERT_OK(vmo.write(block, kMinfsBlockSize, kMinfsBlockSize));
  FillWriteRequest(transaction_handler.get(), abm_block, ibm_block, vmoid.get(), request);
  ASSERT_OK(device.FifoTransaction(request, 2));

  // Try to correct the corrupted superblock.
  zx_status_t status = RepairSuperblock(transaction_handler.get(), &device,
                                        backup.dat_block + backup.block_count, &info);
  ASSERT_EQ(status, ZX_OK);

  // Read back the superblock and backup superblock.
  ASSERT_OK(device.ReadBlock(kSuperblockStart, kMinfsBlockSize, &info));
  ASSERT_OK(device.ReadBlock(kNonFvmSuperblockBackup, kMinfsBlockSize, &backup));

  // Confirm that alloc_*_counts are updated correctly in superblock and backup from bitmaps.
  ASSERT_GT(info.alloc_block_count, 0);
  ASSERT_GT(info.alloc_inode_count, 0);
  ASSERT_GT(backup.alloc_block_count, 0);
  ASSERT_GT(backup.alloc_inode_count, 0);
}

TEST(SuperblockTest, UnsupportedBlockSize) {
  ASSERT_DEATH(([]() {
    Superblock info = {};
    info.block_size = kMinfsBlockSize - 1;
    info.BlockSize();
  }));
}

TEST(SuperblockTest, SupportedBlockSize) {
  ASSERT_NO_DEATH(([]() {
    Superblock info = {};
    info.block_size = kMinfsBlockSize;
    info.BlockSize();
  }));
}

}  // namespace
}  // namespace minfs
