// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Tests minfs backup superblock behavior.

#include <block-client/cpp/fake-device.h>
#include <minfs/fsck.h>
#include <minfs/superblock.h>
#include <unistd.h>
#include <zxtest/zxtest.h>

namespace minfs {
namespace {

using block_client::FakeBlockDevice;
constexpr size_t abm_block = 5;
constexpr size_t ibm_block = 6;
constexpr size_t data_block = 7;
constexpr size_t journal_block = 8;

// Mock TransactionHandler class to be used in superblock tests.
class MockTransactionHandler : public fs::TransactionHandler {
 public:
  MockTransactionHandler(block_client::BlockDevice* device) {
#ifdef __Fuchsia__
    device_ = device;
#endif
  }

  MockTransactionHandler(const MockTransactionHandler&) = delete;
  MockTransactionHandler(MockTransactionHandler&&) = delete;
  MockTransactionHandler& operator=(const MockTransactionHandler&) = delete;
  MockTransactionHandler& operator=(MockTransactionHandler&&) = delete;

  // fs::TransactionHandler Interface.
  uint32_t FsBlockSize() const { return kMinfsBlockSize; }

#ifdef __Fuchsia__
  groupid_t BlockGroupID() final { return 0; }

  uint32_t DeviceBlockSize() const final { return kMinfsBlockSize; }

  zx_status_t Transaction(block_fifo_request_t* requests, size_t count) final {
    return device_->FifoTransaction(requests, count);
  }

#else

  zx_status_t Readblk(blk_t bno, void* data) { return ZX_ERR_NOT_SUPPORTED; }

  zx_status_t Writeblk(blk_t bno, const void* data) { return ZX_ERR_NOT_SUPPORTED; }
#endif

 private:
#ifdef __Fuchsia__
  block_client::BlockDevice* device_;
#endif
};

void CreateAndRegisterVmo(block_client::BlockDevice* device, size_t blocks, zx::vmo* vmo,
                          fuchsia_hardware_block_VmoID* vmoid) {
  fuchsia_hardware_block_BlockInfo info = {};
  ASSERT_OK(device->BlockGetInfo(&info));
  ASSERT_OK(zx::vmo::create(blocks * info.block_size, 0, vmo));
  ASSERT_OK(device->BlockAttachVmo(*vmo, vmoid));
}

void FillSuperblockFields(Superblock* info) {
  info->magic0 = kMinfsMagic0;
  info->magic1 = kMinfsMagic1;
  info->version = kMinfsVersion;
  info->flags = kMinfsFlagClean;
  info->block_size = kMinfsBlockSize;
  info->inode_size = kMinfsInodeSize;
  info->dat_block = data_block;
  info->journal_start_block = journal_block;
  info->ibm_block = ibm_block;
  info->abm_block = abm_block;
  info->block_count = 1;
  info->inode_count = 1;
  info->alloc_block_count = 2;
  info->alloc_inode_count = 2;
}

// Tests the alloc_*_counts bitmap reconstruction.
TEST(SuperblockTest, TestBitmapReconstruction) {
  Superblock info = {};
  FillSuperblockFields(&info);

  FakeBlockDevice device = FakeBlockDevice(100, kMinfsBlockSize);
  auto transaction_handler =
      std::unique_ptr<MockTransactionHandler>(new MockTransactionHandler(&device));

  // Write abm_block and ibm_block.
  uint8_t block[minfs::kMinfsBlockSize];
  memset(block, 0, sizeof(block));

  // Fill up the entire bitmap sparsely with random 1 and 0.
  // 0xFF = 8 bits set.
  block[0] = 0xFF;
  block[30] = 0xFF;
  block[100] = 0xFF;
  block[5000] = 0xFF;

  zx::vmo vmo;
  const size_t kVmoBlocks = 2;
  fuchsia_hardware_block_VmoID vmoid;
  ASSERT_NO_FAILURES(CreateAndRegisterVmo(&device, kVmoBlocks, &vmo, &vmoid));

  // Write the bitmaps on disk.
  ASSERT_OK(vmo.write(block, 0, sizeof(block)));
  block_fifo_request_t request[2];

  request[0].opcode = BLOCKIO_WRITE;
  request[0].vmoid = vmoid.id;
  request[0].length = kVmoBlocks;
  request[0].vmo_offset = 0;
  request[0].dev_offset = abm_block;

  request[1].opcode = BLOCKIO_WRITE;
  request[1].vmoid = vmoid.id;
  request[1].length = kVmoBlocks;
  request[1].vmo_offset = 0;
  request[1].dev_offset = ibm_block;
  ASSERT_OK(device.FifoTransaction(request, 2));

  // Reconstruct alloc_*_counts from respective bitmaps.
  zx_status_t status = ReconstructAllocCounts(transaction_handler.get(), &device, &info);
  ASSERT_EQ(status, ZX_OK);

  // Confirm the alloc_*_counts are updated correctly.
  ASSERT_EQ(32, info.alloc_block_count);
  ASSERT_EQ(32, info.alloc_inode_count);

  // Write all bits unset for abm_block and ibm_block.
  memset(block, 0, sizeof(block));

  // Write the bitmaps on disk.
  ASSERT_OK(vmo.write(block, 0, sizeof(block)));
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
  ASSERT_OK(device.FifoTransaction(request, 2));

  // Reconstruct alloc_*_counts from respective bitmaps.
  status = ReconstructAllocCounts(transaction_handler.get(), &device, &info);
  ASSERT_EQ(status, ZX_OK);

  // Confirm the alloc_*_counts are updated correctly.
  ASSERT_EQ(11, info.alloc_block_count);
  ASSERT_EQ(11, info.alloc_inode_count);
}

}  // namespace
}  // namespace minfs
