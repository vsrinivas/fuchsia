// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <block-client/cpp/fake-device.h>
#include <fbl/auto_call.h>
#include <zxtest/zxtest.h>

#include "src/storage/minfs/bcache.h"
#include "src/storage/minfs/format.h"
#include "src/storage/minfs/lazy_buffer.h"
#include "src/storage/minfs/minfs.h"
#include "src/storage/minfs/minfs_private.h"

namespace minfs {
namespace {

using ::block_client::FakeBlockDevice;

class VnodeMapperTestFixture : public zxtest::Test {
 public:
  const int kNumBlocks = 1 << 20;

  VnodeMapperTestFixture() {
    auto device = std::make_unique<FakeBlockDevice>(kNumBlocks, kMinfsBlockSize);
    ASSERT_TRUE(device);
    std::unique_ptr<Bcache> bcache;
    ASSERT_OK(Bcache::Create(std::move(device), kNumBlocks, &bcache));
    ASSERT_OK(Mkfs(bcache.get()));
    ASSERT_OK(Minfs::Create(std::move(bcache), MountOptions(), &fs_));
    VnodeMinfs::Allocate(fs_.get(), kMinfsTypeFile, &vnode_);
    EXPECT_OK(vnode_->Open(vnode_->ValidateOptions(fs::VnodeConnectionOptions()).value(), nullptr));
  }

  ~VnodeMapperTestFixture() { vnode_->Close(); }

 protected:
  std::unique_ptr<Minfs> fs_;
  fbl::RefPtr<VnodeMinfs> vnode_;
};

using VnodeIndirectMapperTest = VnodeMapperTestFixture;

TEST_F(VnodeIndirectMapperTest, FirstIndirectBlockIsMapped) {
  vnode_->GetMutableInode()->inum[0] = 10;
  VnodeIndirectMapper mapper(vnode_.get());
  zx::status<DeviceBlockRange> device_range = mapper.Map(BlockRange(0, 2));
  ASSERT_OK(device_range.status_value());
  ASSERT_TRUE(device_range.value().IsMapped());
  EXPECT_EQ(fs_->Info().dat_block + 10, device_range.value().block());
  EXPECT_EQ(1, device_range.value().count());
}

TEST_F(VnodeIndirectMapperTest, CoalescedBlocks) {
  vnode_->GetMutableInode()->inum[0] = 10;
  vnode_->GetMutableInode()->inum[1] = 11;
  VnodeIndirectMapper mapper(vnode_.get());
  zx::status<DeviceBlockRange> device_range = mapper.Map(BlockRange(0, 2));
  ASSERT_OK(device_range.status_value());
  EXPECT_EQ(fs_->Info().dat_block + 10, device_range.value().block());
  EXPECT_EQ(2, device_range.value().count());
}

TEST_F(VnodeIndirectMapperTest, LastIndirectBlockIsMapped) {
  vnode_->GetMutableInode()->inum[kMinfsIndirect - 1] = 17;
  VnodeIndirectMapper mapper(vnode_.get());
  zx::status<DeviceBlockRange> device_range =
      mapper.Map(BlockRange(kMinfsIndirect - 1, kMinfsIndirect));
  ASSERT_OK(device_range.status_value());
  EXPECT_EQ(fs_->Info().dat_block + 17, device_range.value().block());
  EXPECT_EQ(1, device_range.value().count());
}

TEST_F(VnodeIndirectMapperTest, IndirectBlocksAreUnmapped) {
  vnode_->GetMutableInode()->inum[kMinfsIndirect - 1] = 17;
  VnodeIndirectMapper mapper(vnode_.get());
  zx::status<DeviceBlockRange> device_range = mapper.Map(BlockRange(3, kMinfsIndirect));
  ASSERT_OK(device_range.status_value());
  EXPECT_FALSE(device_range.value().IsMapped());
  EXPECT_EQ(kMinfsIndirect - 3 - 1, device_range.value().count());
}

TEST_F(VnodeIndirectMapperTest, DoubleIndirectBlockIsMapped) {
  vnode_->GetMutableInode()->dinum[0] = 17;
  VnodeIndirectMapper mapper(vnode_.get());
  zx::status<DeviceBlockRange> device_range =
      mapper.Map(BlockRange(kMinfsIndirect, kMinfsIndirect + 1));
  ASSERT_OK(device_range.status_value());
  EXPECT_EQ(fs_->Info().dat_block + 17, device_range.value().block());
  EXPECT_EQ(1, device_range.value().count());
}

TEST_F(VnodeIndirectMapperTest, DoubleIndirectFirstLeafBlockIsMapped) {
  vnode_->GetMutableInode()->dinum[0] = 17;
  blk_t buffer[kMinfsDirectPerIndirect] = {18};
  ASSERT_OK(fs_->GetMutableBcache()->Writeblk(fs_->Info().dat_block + 17, buffer));
  VnodeIndirectMapper mapper(vnode_.get());
  uint64_t block = kMinfsIndirect + kMinfsDoublyIndirect;
  zx::status<DeviceBlockRange> device_range = mapper.Map(BlockRange(block, block + 1));
  ASSERT_OK(device_range.status_value());
  EXPECT_EQ(fs_->Info().dat_block + 18, device_range.value().block());
  EXPECT_EQ(1, device_range.value().count());
}

TEST_F(VnodeIndirectMapperTest, DoubleIndirectLastLeafBlockIsMapped) {
  vnode_->GetMutableInode()->dinum[0] = 17;
  blk_t buffer[kMinfsDirectPerIndirect] = {};
  buffer[kMinfsDirectPerIndirect - 1] = 21;
  ASSERT_OK(fs_->GetMutableBcache()->Writeblk(fs_->Info().dat_block + 17, buffer));
  VnodeIndirectMapper mapper(vnode_.get());
  uint64_t block =
      kMinfsIndirect + kMinfsDoublyIndirect + kMinfsDirectPerIndirect * kMinfsDoublyIndirect - 1;
  zx::status<DeviceBlockRange> device_range = mapper.Map(BlockRange(block, block + 1));
  ASSERT_OK(device_range.status_value());
  EXPECT_EQ(fs_->Info().dat_block + 21, device_range.value().block());
  EXPECT_EQ(1, device_range.value().count());
}

TEST_F(VnodeIndirectMapperTest, BlockOutOfRange) {
  VnodeIndirectMapper mapper(vnode_.get());
  uint64_t block =
      kMinfsIndirect + kMinfsDoublyIndirect + kMinfsDirectPerIndirect * kMinfsDoublyIndirect;
  zx::status<DeviceBlockRange> device_range = mapper.Map(BlockRange(block, block + 1));
  EXPECT_STATUS(device_range.status_value(), ZX_ERR_OUT_OF_RANGE);
}

class FakeTransaction : public PendingWork {
 public:
  static constexpr uint64_t kFirstBlock = 31;

  FakeTransaction(Bcache* bcache) : bcache_(*bcache) {}

  void EnqueueMetadata(storage::Operation operation, storage::BlockBuffer* buffer) override {
    uint64_t length = operation.length;
    uint8_t data[kMinfsBlockSize];
    uint64_t vmo_offset = operation.vmo_offset * kMinfsBlockSize;
    uint64_t dev_block = operation.dev_offset;
    while (length > 0) {
      if (operation.type == storage::OperationType::kRead) {
        ASSERT_OK(bcache_.Readblk(static_cast<blk_t>(dev_block), data));
        zx_vmo_write(buffer->Vmo(), data, vmo_offset, kMinfsBlockSize);
      } else {
        zx_vmo_read(buffer->Vmo(), data, vmo_offset, kMinfsBlockSize);
        ASSERT_OK(bcache_.Writeblk(static_cast<blk_t>(dev_block), data));
      }
      ++vmo_offset;
      ++dev_block;
      --length;
    }
    ++write_count_;
  }
  void EnqueueData(storage::Operation operation, storage::BlockBuffer* buffer) override {
    EnqueueMetadata(operation, buffer);
  }
  size_t AllocateBlock() override { return block_++; }
  void DeallocateBlock(size_t block) override { deallocated_blocks_.push_back(block); }

  int write_count() const { return write_count_; }
  std::vector<size_t>& deallocated_blocks() { return deallocated_blocks_; }

 private:
  Bcache& bcache_;
  uint64_t block_ = kFirstBlock;
  int write_count_ = 0;
  std::vector<size_t> deallocated_blocks_;
};

TEST_F(VnodeIndirectMapperTest, MapForWriteAllocatesBlock) {
  VnodeIndirectMapper mapper(vnode_.get());
  FakeTransaction transaction(fs_->GetMutableBcache());
  bool allocated = false;
  zx::status<DeviceBlockRange> device_range =
      mapper.MapForWrite(&transaction, BlockRange(10, 10 + 2), &allocated);
  ASSERT_OK(device_range.status_value());
  EXPECT_EQ(fs_->Info().dat_block + FakeTransaction::kFirstBlock, device_range.value().block());
  EXPECT_EQ(1, device_range.value().count());
  EXPECT_TRUE(allocated);
}

using VnodeMapperTest = VnodeMapperTestFixture;

TEST_F(VnodeMapperTest, VnodeMapperDirectBlocksAreMapped) {
  vnode_->GetMutableInode()->dnum[0] = 17;
  VnodeMapper mapper(vnode_.get());
  zx::status<std::pair<blk_t, uint64_t>> mapping = mapper.MapToBlk(BlockRange(0, 2));
  ASSERT_OK(mapping.status_value());
  EXPECT_EQ(17, mapping.value().first);
  EXPECT_EQ(1, mapping.value().second);
  zx::status<DeviceBlockRange> device_range = mapper.Map(BlockRange(0, 2));
  ASSERT_OK(device_range.status_value());
  ASSERT_TRUE(device_range.value().IsMapped());
  EXPECT_EQ(fs_->Info().dat_block + 17, device_range.value().block());
  EXPECT_EQ(1, device_range.value().count());
}

TEST_F(VnodeMapperTest, VnodeMapperContiguousDirectBlocksAreCoalesced) {
  vnode_->GetMutableInode()->dnum[0] = 17;
  vnode_->GetMutableInode()->dnum[1] = 18;
  vnode_->GetMutableInode()->dnum[2] = 20;
  VnodeMapper mapper(vnode_.get());
  zx::status<DeviceBlockRange> device_range = mapper.Map(BlockRange(0, 3));
  ASSERT_OK(device_range.status_value());
  ASSERT_TRUE(device_range.value().IsMapped());
  EXPECT_EQ(fs_->Info().dat_block + 17, device_range.value().block());
  EXPECT_EQ(2, device_range.value().count());
}

TEST_F(VnodeMapperTest, VnodeMapperIndirectBlocksAreMapped) {
  vnode_->GetMutableInode()->inum[0] = 17;
  blk_t buffer[kMinfsDirectPerIndirect] = {19};
  ASSERT_OK(fs_->GetMutableBcache()->Writeblk(fs_->Info().dat_block + 17, buffer));
  VnodeMapper mapper(vnode_.get());
  uint64_t block = kMinfsDirect;
  zx::status<DeviceBlockRange> device_range = mapper.Map(BlockRange(block, block + 2));
  ASSERT_OK(device_range.status_value());
  ASSERT_TRUE(device_range.value().IsMapped());
  EXPECT_EQ(fs_->Info().dat_block + 19, device_range.value().block());
  EXPECT_EQ(1, device_range.value().count());
}

TEST_F(VnodeMapperTest, VnodeMapperDoubleIndirectBlocksAreMapped) {
  vnode_->GetMutableInode()->dinum[0] = 17;
  blk_t buffer[kMinfsDirectPerIndirect] = {19};
  ASSERT_OK(fs_->GetMutableBcache()->Writeblk(fs_->Info().dat_block + 17, buffer));
  buffer[0] = 37;
  buffer[1] = 38;
  ASSERT_OK(fs_->GetMutableBcache()->Writeblk(fs_->Info().dat_block + 19, buffer));
  VnodeMapper mapper(vnode_.get());
  uint64_t block_count = 3;
  uint64_t block = kMinfsDirect + kMinfsIndirect * kMinfsDirectPerIndirect;
  zx::status<DeviceBlockRange> device_range = mapper.Map(BlockRange(block, block + block_count));
  ASSERT_OK(device_range.status_value());
  ASSERT_TRUE(device_range.value().IsMapped());
  EXPECT_EQ(fs_->Info().dat_block + 37, device_range.value().block());
  EXPECT_EQ(2, device_range.value().count());
}

using VnodeIteratorTest = VnodeMapperTestFixture;

TEST_F(VnodeIteratorTest, WholeFileIsSparse) {
  VnodeMapper mapper(vnode_.get());
  VnodeIterator iterator;
  ASSERT_OK(iterator.Init(&mapper, /*transaction=*/nullptr, 0));
  EXPECT_EQ(0, iterator.Blk());
  // The entire file should be sparse.
  EXPECT_EQ(kMinfsDirect, iterator.GetContiguousBlockCount());
  EXPECT_OK(iterator.Advance(kMinfsDirect));
  EXPECT_EQ(kMinfsIndirect * kMinfsDirectPerIndirect, iterator.GetContiguousBlockCount());
  EXPECT_EQ(kMinfsDirect, iterator.file_block());
  EXPECT_OK(iterator.Advance(kMinfsIndirect * kMinfsDirectPerIndirect));
  EXPECT_EQ(kMinfsDoublyIndirect * kMinfsDirectPerIndirect * kMinfsDirectPerIndirect,
            iterator.GetContiguousBlockCount());
  EXPECT_OK(
      iterator.Advance(kMinfsDoublyIndirect * kMinfsDirectPerIndirect * kMinfsDirectPerIndirect));
  EXPECT_EQ(0, iterator.GetContiguousBlockCount());
}

TEST_F(VnodeIteratorTest, SparseFirstIndirectBlockCoalescedCorrectly) {
  vnode_->GetMutableInode()->inum[1] = 17;
  VnodeMapper mapper(vnode_.get());
  VnodeIterator iterator;
  ASSERT_OK(iterator.Init(&mapper, /*transaction=*/nullptr, kMinfsDirect));
  EXPECT_EQ(2048, iterator.GetContiguousBlockCount(std::numeric_limits<uint64_t>::max()));
}

TEST_F(VnodeIteratorTest, AdvanceBeyondMaximumFails) {
  VnodeMapper mapper(vnode_.get());
  VnodeIterator iterator;
  ASSERT_OK(iterator.Init(&mapper, /*transaction=*/nullptr, VnodeMapper::kMaxBlocks));
  EXPECT_STATUS(iterator.Advance(1), ZX_ERR_BAD_STATE);
}

TEST_F(VnodeIteratorTest, SetDirectBlock) {
  VnodeMapper mapper(vnode_.get());
  VnodeIterator iterator;
  FakeTransaction transaction(fs_->GetMutableBcache());
  ASSERT_OK(iterator.Init(&mapper, &transaction, 0));
  EXPECT_OK(iterator.SetBlk(1));
  ASSERT_OK(iterator.Flush());
  EXPECT_EQ(1, vnode_->GetInode()->dnum[0]);
}

TEST_F(VnodeIteratorTest, SetIndirectBlock) {
  VnodeMapper mapper(vnode_.get());
  VnodeIterator iterator;
  FakeTransaction transaction(fs_->GetMutableBcache());
  ASSERT_OK(iterator.Init(&mapper, &transaction, kMinfsDirect));
  EXPECT_OK(iterator.SetBlk(1));
  EXPECT_OK(iterator.Flush());
  EXPECT_EQ(FakeTransaction::kFirstBlock, vnode_->GetInode()->inum[0]);
  // Check the indirect node was flushed.
  blk_t data[kMinfsDirectPerIndirect];
  ASSERT_OK(
      fs_->GetMutableBcache()->Readblk(fs_->Info().dat_block + FakeTransaction::kFirstBlock, data));
  EXPECT_EQ(1, data[0]);
}

TEST_F(VnodeIteratorTest, AllocateLastBlock) {
  VnodeMapper mapper(vnode_.get());
  VnodeIterator iterator;
  FakeTransaction transaction(fs_->GetMutableBcache());
  // Allocate the very last block.
  ASSERT_OK(iterator.Init(&mapper, &transaction,
                          kMinfsDirect +
                              (kMinfsIndirect + kMinfsDoublyIndirect * kMinfsDirectPerIndirect) *
                                  kMinfsDirectPerIndirect -
                              1));
  EXPECT_OK(iterator.SetBlk(1));
  EXPECT_OK(iterator.Flush());
  // Check the doubly indirect node was flushed.
  EXPECT_NE(0, vnode_->GetInode()->dinum[kMinfsDoublyIndirect - 1]);
  blk_t data[kMinfsDirectPerIndirect];
  ASSERT_OK(fs_->GetMutableBcache()->Readblk(
      fs_->Info().dat_block + vnode_->GetInode()->dinum[kMinfsDoublyIndirect - 1], data));
  EXPECT_NE(0, data[kMinfsDirectPerIndirect - 1]);
  ASSERT_OK(fs_->GetMutableBcache()->Readblk(
      fs_->Info().dat_block + data[kMinfsDirectPerIndirect - 1], data));
  EXPECT_EQ(1, data[kMinfsDirectPerIndirect - 1]);
}

TEST_F(VnodeIteratorTest, IndirectBlockDeallocatedWhenCleared) {
  VnodeMapper mapper(vnode_.get());
  // Test to ensure that indirect blocks are freed rather than written when they have no more
  // entries.
  blk_t blocks[2];
  blk_t indirect_blocks[3];
  blk_t data[kMinfsDirectPerIndirect];
  FakeTransaction transaction(fs_->GetMutableBcache());
  {
    // First allocate two blocks in the double-indirect region. We should end up with something
    // like:
    //
    //                                         inode.dinum (a)
    //                                              |
    //                                              v
    //                                        |b|c| ... |
    //                                         | |
    //                                         | +-----------------+
    //                                         v                   v
    //                                        |x| ... |           |y| ... |
    //
    // where a, b and c are the indirect blocks allocated. These are tracked by |indirect_blocks|. x
    // and y are the direct blocks and are tracked by |blocks|.

    VnodeIterator iterator;
    ASSERT_OK(iterator.Init(&mapper, &transaction,
                            kMinfsDirect + kMinfsIndirect * kMinfsDirectPerIndirect));
    blocks[0] = static_cast<blk_t>(transaction.AllocateBlock());
    blocks[1] = static_cast<blk_t>(transaction.AllocateBlock());
    EXPECT_OK(iterator.SetBlk(blocks[0]));
    EXPECT_OK(iterator.Advance(kMinfsDirectPerIndirect));
    EXPECT_OK(iterator.SetBlk(blocks[1]));
    EXPECT_OK(iterator.Flush());

    // Wait for the flush to make it through to the device.
    sync_completion_t synced;
    fs_->Sync([&](zx_status_t status) { sync_completion_signal(&synced); });
    EXPECT_OK(sync_completion_wait(&synced, zx::sec(5).get()));

    // Check the block pointers.
    indirect_blocks[0] = vnode_->GetInode()->dinum[kMinfsDoublyIndirect - 1];
    EXPECT_NE(0, indirect_blocks[0]);
    ASSERT_OK(fs_->GetMutableBcache()->Readblk(fs_->Info().dat_block + indirect_blocks[0], data));
    indirect_blocks[1] = data[0];
    indirect_blocks[2] = data[1];
    EXPECT_NE(0, indirect_blocks[1]);
    EXPECT_NE(1, indirect_blocks[2]);
    ASSERT_OK(fs_->GetMutableBcache()->Readblk(fs_->Info().dat_block + indirect_blocks[1], data));
    EXPECT_EQ(blocks[0], data[0]);
    ASSERT_OK(fs_->GetMutableBcache()->Readblk(fs_->Info().dat_block + indirect_blocks[2], data));
    EXPECT_EQ(blocks[1], data[0]);
  }

  const int write_count_for_set_up = transaction.write_count();

  // Now with a new iterator, zero those entries out and flush, and it should deallocate all
  // blocks.
  VnodeIterator iterator;
  ASSERT_OK(iterator.Init(&mapper, &transaction,
                          kMinfsDirect + kMinfsIndirect * kMinfsDirectPerIndirect));
  EXPECT_OK(iterator.SetBlk(0));
  EXPECT_OK(iterator.Advance(kMinfsDirectPerIndirect));
  // That should have caused the first of the indirect blocks to be freed.
  ASSERT_EQ(1, transaction.deallocated_blocks().size());
  EXPECT_EQ(indirect_blocks[1], transaction.deallocated_blocks()[0]);
  // But nothing should have been written yet via FakeTransaction because the iterator shouldn't
  // have written any blocks yet.
  EXPECT_EQ(write_count_for_set_up, transaction.write_count());

  // Flush now.
  EXPECT_OK(iterator.Flush());
  ASSERT_OK(fs_->GetMutableBcache()->Readblk(fs_->Info().dat_block + indirect_blocks[0], data));
  EXPECT_EQ(0, data[0]);
  EXPECT_EQ(indirect_blocks[2], data[1]);

  // Deallocate the second block and advance.
  EXPECT_OK(iterator.SetBlk(0));
  // Advance to the end.
  EXPECT_OK(iterator.Advance(VnodeMapper::kMaxBlocks - iterator.file_block()));
  // All blocks should have been deallocated now.
  ASSERT_EQ(3, transaction.deallocated_blocks().size());
  EXPECT_EQ(indirect_blocks[2], transaction.deallocated_blocks()[1]);
  EXPECT_EQ(indirect_blocks[0], transaction.deallocated_blocks()[2]);
  EXPECT_EQ(0, vnode_->GetInode()->dinum[kMinfsDoublyIndirect - 1]);
}

}  // namespace
}  // namespace minfs
