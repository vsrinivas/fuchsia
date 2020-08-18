// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "blobfs/blobfs_inspector.h"

#include <iostream>
#include <memory>

#include <blobfs/common.h>
#include <blobfs/format.h>
#include <disk_inspector/buffer_factory.h>
#include <fs/journal/format.h>
#include <fs/journal/initializer.h>
#include <fs/transaction/legacy_transaction_handler.h>
#include <gtest/gtest.h>
#include <safemath/checked_math.h>
#include <storage/buffer/array_buffer.h>

namespace blobfs {
namespace {

constexpr uint64_t kBlockCount = 1 << 10;

class FakeTransactionHandler : public fs::LegacyTransactionHandler {
 public:
  explicit FakeTransactionHandler(std::unique_ptr<storage::ArrayBuffer> fake_device)
      : fake_device_(std::move(fake_device)) {}
  FakeTransactionHandler(const FakeTransactionHandler&) = delete;
  FakeTransactionHandler(FakeTransactionHandler&&) = default;
  FakeTransactionHandler& operator=(const FakeTransactionHandler&) = delete;
  FakeTransactionHandler& operator=(FakeTransactionHandler&&) = default;

  // TransactionHandler interface:
  uint32_t FsBlockSize() const final { return fake_device_->BlockSize(); }

  uint64_t BlockNumberToDevice(uint64_t block_num) const final { return block_num; }

  zx_status_t RunOperation(const storage::Operation& operation,
                           storage::BlockBuffer* buffer) final {
    ValidateOperation(operation, buffer);
    switch (operation.type) {
      case storage::OperationType::kRead:
        memcpy(buffer->Data(operation.vmo_offset), fake_device_->Data(operation.dev_offset),
               operation.length * fake_device_->BlockSize());
        break;
      case storage::OperationType::kWrite:
        memcpy(fake_device_->Data(operation.dev_offset), buffer->Data(operation.vmo_offset),
               operation.length * fake_device_->BlockSize());
        break;
      default:
        return ZX_ERR_NOT_SUPPORTED;
    }
    return ZX_OK;
  }

  uint32_t DeviceBlockSize() const final { return fake_device_->BlockSize(); }

  block_client::BlockDevice* GetDevice() final { return nullptr; }

  zx_status_t Transaction(block_fifo_request_t* requests, size_t count) final {
    return ZX_ERR_NOT_SUPPORTED;
  }

  void ValidateOperation(const storage::Operation& operation, storage::BlockBuffer* buffer) {
    ASSERT_NE(fake_device_, nullptr);
    ASSERT_GE(buffer->capacity(), operation.vmo_offset + operation.length)
        << "Operation goes past input buffer length";
    ASSERT_GE(fake_device_->capacity(), operation.dev_offset + operation.length)
        << "Operation goes past device buffer length";

    ASSERT_NE(operation.type, storage::OperationType::kTrim) << "Trim operation is not supported\n";
  }

  storage::BlockBuffer* GetDeviceBuffer() { return fake_device_.get(); }

 private:
  std::unique_ptr<storage::ArrayBuffer> fake_device_;
};

class ArrayBufferFactory : public disk_inspector::BufferFactory {
 public:
  explicit ArrayBufferFactory(uint32_t block_size) : block_size_(block_size) {}
  ArrayBufferFactory(const ArrayBufferFactory&) = delete;
  ArrayBufferFactory(ArrayBufferFactory&&) = default;
  ArrayBufferFactory& operator=(const ArrayBufferFactory&) = delete;
  ArrayBufferFactory& operator=(ArrayBufferFactory&&) = default;

  fit::result<std::unique_ptr<storage::BlockBuffer>, zx_status_t> CreateBuffer(
      size_t capacity) const final {
    return fit::ok(std::make_unique<storage::ArrayBuffer>(capacity, block_size_));
  }

 private:
  uint32_t block_size_;
};

// Initialize a FakeTrnasactionHandler backed by a buffer representing a
// a fresh Blobfs partition and journal entries.
void CreateFakeBlobfsHandler(std::unique_ptr<FakeTransactionHandler>* handler) {
  auto device = std::make_unique<storage::ArrayBuffer>(kBlockCount, kBlobfsBlockSize);

  // Superblock.
  Superblock superblock;
  InitializeSuperblock(kBlockCount, &superblock);
  memcpy(device->Data(kSuperblockOffset), &superblock, sizeof(superblock));

  // Allocation bitmap.
  RawBitmap block_bitmap;
  ASSERT_EQ(block_bitmap.Reset(BlockMapBlocks(superblock) * kBlobfsBlockBits), ZX_OK);
  block_bitmap.Set(0, kStartBlockMinimum);
  uint64_t bitmap_length = BlockMapBlocks(superblock) * kBlobfsBlockSize;
  memcpy(device->Data(BlockMapStartBlock(superblock)), GetRawBitmapData(block_bitmap, 0),
         bitmap_length);

  // Node map.
  uint64_t nodemap_length = NodeMapBlocks(superblock) * kBlobfsBlockSize;
  memset(device->Data(NodeMapStartBlock(superblock)), 0, nodemap_length);

  // Journal.
  fs::WriteBlocksFn write_blocks_fn = [&device, &superblock](fbl::Span<const uint8_t> buffer,
                                                             uint64_t block_offset,
                                                             uint64_t block_count) {
    uint64_t size = safemath::CheckMul<uint64_t>(block_count, kBlobfsBlockSize).ValueOrDie();
    ZX_ASSERT((block_offset + block_count) <= JournalBlocks(superblock));
    ZX_ASSERT(buffer.size() >= size);
    memcpy(device->Data(JournalStartBlock(superblock) + block_offset), buffer.data(), size);
    return ZX_OK;
  };
  ASSERT_EQ(fs::MakeJournal(JournalBlocks(superblock), write_blocks_fn), ZX_OK);

  *handler = std::make_unique<FakeTransactionHandler>(std::move(device));
}

// Initialize a BlobfsInspector from an zero-ed out block device. This simulates
// corruption to various metadata.
void CreateBadFakeBlobfsHandler(std::unique_ptr<FakeTransactionHandler>* handler) {
  auto device = std::make_unique<storage::ArrayBuffer>(kBlockCount, kBlobfsBlockSize);
  memset(device->Data(0), 0, kBlockCount * kBlobfsBlockSize);
  *handler = std::make_unique<FakeTransactionHandler>(std::move(device));
}

void CreateBlobfsInspector(std::unique_ptr<FakeTransactionHandler> handler,
                           std::unique_ptr<BlobfsInspector>* out) {
  auto buffer_factory = std::make_unique<ArrayBufferFactory>(kBlobfsBlockSize);
  auto result = BlobfsInspector::Create(std::move(handler), std::move(buffer_factory));
  ASSERT_TRUE(result.is_ok());
  *out = std::move(result.value());
}

TEST(BlobfsInspector, CreateWithoutError) {
  std::unique_ptr<FakeTransactionHandler> handler;
  CreateFakeBlobfsHandler(&handler);
  std::unique_ptr<BlobfsInspector> inspector;
  CreateBlobfsInspector(std::move(handler), &inspector);
}

TEST(BlobfsInspector, CreateWithoutErrorOnBadSuperblock) {
  std::unique_ptr<FakeTransactionHandler> handler;
  CreateBadFakeBlobfsHandler(&handler);
  std::unique_ptr<BlobfsInspector> inspector;
  CreateBlobfsInspector(std::move(handler), &inspector);
}

TEST(BlobfsInspector, InspectSuperblock) {
  std::unique_ptr<FakeTransactionHandler> handler;
  CreateFakeBlobfsHandler(&handler);
  std::unique_ptr<BlobfsInspector> inspector;
  CreateBlobfsInspector(std::move(handler), &inspector);

  Superblock sb = inspector->InspectSuperblock();

  EXPECT_EQ(kBlobfsMagic0, sb.magic0);
  EXPECT_EQ(kBlobfsMagic1, sb.magic1);
  EXPECT_EQ(kBlobfsVersion, sb.version);
  EXPECT_EQ(kBlobFlagClean, sb.flags);
  EXPECT_EQ(kBlobfsBlockSize, sb.block_size);
  EXPECT_EQ(1ul, sb.alloc_block_count);
  EXPECT_EQ(0ul, sb.alloc_inode_count);
  EXPECT_EQ(0ul, sb.reserved2);
}

TEST(BlobfsInspector, GetInodeCount) {
  std::unique_ptr<FakeTransactionHandler> handler;
  CreateFakeBlobfsHandler(&handler);
  std::unique_ptr<BlobfsInspector> inspector;
  CreateBlobfsInspector(std::move(handler), &inspector);

  Superblock sb = inspector->InspectSuperblock();
  EXPECT_EQ(sb.inode_count, inspector->GetInodeCount());
}

TEST(BlobfsInspector, InspectInode) {
  std::unique_ptr<FakeTransactionHandler> handler;
  CreateFakeBlobfsHandler(&handler);

  auto* superblock =
      reinterpret_cast<Superblock*>(handler->GetDeviceBuffer()->Data(kSuperblockOffset));
  superblock->alloc_inode_count = 2;

  // Create the first node to be an inode.
  auto inode =
      reinterpret_cast<Inode*>(handler->GetDeviceBuffer()->Data(NodeMapStartBlock(*superblock)));
  inode[0].header.flags = kBlobFlagAllocated;
  inode[0].block_count = 5;
  inode[0].extent_count = 42;

  // Create the second node to be an extent.
  auto extent = reinterpret_cast<ExtentContainer*>(
      handler->GetDeviceBuffer()->Data(NodeMapStartBlock(*superblock)));
  extent[1].header.flags = kBlobFlagAllocated | kBlobFlagExtentContainer;
  extent[1].previous_node = 10;
  extent[1].extent_count = 123;

  std::unique_ptr<BlobfsInspector> inspector;
  CreateBlobfsInspector(std::move(handler), &inspector);

  Superblock sb = inspector->InspectSuperblock();
  // The fresh Blobfs device should have 2 allocated inodes.
  ASSERT_EQ(2ul, sb.alloc_inode_count);

  auto result = inspector->InspectInodeRange(0, 3);
  ASSERT_TRUE(result.is_ok());
  std::vector<Inode> inodes = std::move(result.value());
  EXPECT_TRUE(inodes[0].header.IsAllocated());
  EXPECT_TRUE(inodes[0].header.IsInode());
  EXPECT_EQ(5u, inodes[0].block_count);
  EXPECT_EQ(42u, inodes[0].extent_count);

  EXPECT_TRUE(inodes[1].header.IsAllocated());
  EXPECT_FALSE(inodes[1].header.IsInode());
  EXPECT_EQ(10u, inodes[1].AsExtentContainer()->previous_node);
  EXPECT_EQ(123u, inodes[1].AsExtentContainer()->extent_count);

  EXPECT_FALSE(inodes[2].header.IsAllocated());
}

TEST(BlobfsInspector, InspectJournalSuperblock) {
  std::unique_ptr<FakeTransactionHandler> handler;
  CreateFakeBlobfsHandler(&handler);
  std::unique_ptr<BlobfsInspector> inspector;
  CreateBlobfsInspector(std::move(handler), &inspector);

  auto result = inspector->InspectJournalSuperblock();
  ASSERT_TRUE(result.is_ok());
  fs::JournalInfo journal_info = std::move(result.value());

  EXPECT_EQ(fs::kJournalMagic, journal_info.magic);
  EXPECT_EQ(0u, journal_info.start_block);
}

TEST(BlobfsInspector, GetJournalEntryCount) {
  std::unique_ptr<FakeTransactionHandler> handler;
  CreateFakeBlobfsHandler(&handler);
  std::unique_ptr<BlobfsInspector> inspector;
  CreateBlobfsInspector(std::move(handler), &inspector);

  Superblock sb = inspector->InspectSuperblock();
  uint64_t expected_count = JournalBlocks(sb) - fs::kJournalMetadataBlocks;
  EXPECT_EQ(expected_count, inspector->GetJournalEntryCount());
}

// This ends up being a special case because we group both the journal superblock
// and the journal entries in a single buffer, so we cannot just naively subtract
// the number of superblocks from the size of the buffer in the case in which
// the buffer is uninitialized/have capacity of zero.
TEST(BlobfsInspector, GetJournalEntryCountWithNoJournalBlocks) {
  std::unique_ptr<FakeTransactionHandler> handler;
  CreateBadFakeBlobfsHandler(&handler);
  std::unique_ptr<BlobfsInspector> inspector;
  CreateBlobfsInspector(std::move(handler), &inspector);
  EXPECT_EQ(0ul, inspector->GetJournalEntryCount());
}

template <typename T>
void LoadAndUnwrapJournalEntry(BlobfsInspector* inspector, uint64_t index, T* out_value) {
  auto result = inspector->InspectJournalEntryAs<T>(index);
  ASSERT_TRUE(result.is_ok());
  *out_value = std::move(result.value());
}

TEST(BlobfsInspector, InspectJournalEntryAs) {
  std::unique_ptr<FakeTransactionHandler> handler;
  CreateFakeBlobfsHandler(&handler);

  Superblock superblock =
      *reinterpret_cast<Superblock*>(handler->GetDeviceBuffer()->Data(kSuperblockOffset));

  uint64_t journal_entry_start = JournalStartBlock(superblock) + fs::kJournalMetadataBlocks;

  // Write header to backing buffer.
  auto header_ptr = reinterpret_cast<fs::JournalHeaderBlock*>(
      handler->GetDeviceBuffer()->Data(journal_entry_start));
  header_ptr->prefix.magic = fs::kJournalEntryMagic;
  header_ptr->prefix.sequence_number = 0;
  header_ptr->prefix.flags = fs::kJournalPrefixFlagHeader;
  header_ptr->payload_blocks = 2;

  // Write commit to backing buffer.
  auto commit_ptr = reinterpret_cast<fs::JournalCommitBlock*>(
      handler->GetDeviceBuffer()->Data(journal_entry_start + 3));
  commit_ptr->prefix.magic = fs::kJournalEntryMagic;
  commit_ptr->prefix.sequence_number = 0;
  commit_ptr->prefix.flags = fs::kJournalPrefixFlagCommit;

  std::unique_ptr<BlobfsInspector> inspector;
  CreateBlobfsInspector(std::move(handler), &inspector);

  // First four entry blocks should be header, payload, payload, commit.
  fs::JournalHeaderBlock header;
  LoadAndUnwrapJournalEntry(inspector.get(), 0, &header);
  EXPECT_EQ(fs::kJournalEntryMagic, header.prefix.magic);
  EXPECT_EQ(0ul, header.prefix.sequence_number);
  EXPECT_EQ(fs::kJournalPrefixFlagHeader, header.prefix.flags);
  EXPECT_EQ(2ul, header.payload_blocks);

  fs::JournalPrefix prefix;
  LoadAndUnwrapJournalEntry(inspector.get(), 1, &prefix);
  EXPECT_NE(fs::kJournalEntryMagic, prefix.magic);

  LoadAndUnwrapJournalEntry(inspector.get(), 2, &prefix);
  EXPECT_NE(fs::kJournalEntryMagic, prefix.magic);

  fs::JournalCommitBlock commit;
  LoadAndUnwrapJournalEntry(inspector.get(), 3, &commit);
  EXPECT_EQ(fs::kJournalEntryMagic, commit.prefix.magic);
  EXPECT_EQ(0ul, commit.prefix.sequence_number);
  EXPECT_EQ(fs::kJournalPrefixFlagCommit, commit.prefix.flags);
}

TEST(BlobfsInspector, InspectDataBlockAllocatedInRange) {
  std::unique_ptr<FakeTransactionHandler> handler;
  CreateFakeBlobfsHandler(&handler);
  Superblock superblock =
      *reinterpret_cast<Superblock*>(handler->GetDeviceBuffer()->Data(kSuperblockOffset));

  uint64_t bytes_to_set = 10;
  uint64_t bits_to_sample = bytes_to_set * 8;

  // Set alternating bits to true.
  void* block_map_start = handler->GetDeviceBuffer()->Data(BlockMapStartBlock(superblock));
  memset(block_map_start, 0xaa, bytes_to_set);

  std::unique_ptr<BlobfsInspector> inspector;
  CreateBlobfsInspector(std::move(handler), &inspector);

  auto result = inspector->InspectDataBlockAllocatedInRange(0, bits_to_sample);
  ASSERT_TRUE(result.is_ok());

  std::vector<uint64_t> allocated_indices = std::move(result.value());

  ASSERT_EQ(allocated_indices.size(), bits_to_sample / 2);
  for (uint32_t i = 0; i < allocated_indices.size(); ++i) {
    EXPECT_EQ((2u * i) + 1u, allocated_indices[i]);
  }
}

TEST(BlobfsInspector, WriteSuperblock) {
  std::unique_ptr<FakeTransactionHandler> handler;
  CreateFakeBlobfsHandler(&handler);
  std::unique_ptr<BlobfsInspector> inspector;
  CreateBlobfsInspector(std::move(handler), &inspector);

  Superblock sb = inspector->InspectSuperblock();
  // Test original values are correct.
  EXPECT_EQ(kBlobfsMagic0, sb.magic0);
  EXPECT_EQ(kBlobfsMagic1, sb.magic1);
  EXPECT_EQ(kBlobfsVersion, sb.version);

  // Edit values and write.
  sb.magic0 = 0;
  sb.version = 0;
  auto result = inspector->WriteSuperblock(sb);
  ASSERT_TRUE(result.is_ok());

  // Test if superblock is saved in memory.
  Superblock edit_sb = inspector->InspectSuperblock();
  EXPECT_EQ(0ul, edit_sb.magic0);
  EXPECT_EQ(kBlobfsMagic1, edit_sb.magic1);
  EXPECT_EQ(0u, edit_sb.version);

  // Test reloading from disk.
  ASSERT_EQ(inspector->ReloadSuperblock(), ZX_OK);
  Superblock reload_sb = inspector->InspectSuperblock();
  EXPECT_EQ(0ul, reload_sb.magic0);
  EXPECT_EQ(kBlobfsMagic1, reload_sb.magic1);
  EXPECT_EQ(0u, reload_sb.version);
}

std::vector<Inode> AlternateAddInodesAndExtentContainers(uint64_t inode_count) {
  std::vector<Inode> write_inodes;
  for (uint32_t i = 0; i < inode_count; ++i) {
    Inode inode = {};
    if (i % 2 == 0) {
      inode.header.flags = kBlobFlagAllocated;
      inode.block_count = i;
    } else {
      inode.AsExtentContainer()->header.flags = kBlobFlagAllocated | kBlobFlagExtentContainer;
      inode.AsExtentContainer()->previous_node = i;
    }
    write_inodes.emplace_back(inode);
  }
  return write_inodes;
}

TEST(BlobfsInspector, WriteInodes) {
  std::unique_ptr<FakeTransactionHandler> handler;
  CreateFakeBlobfsHandler(&handler);
  std::unique_ptr<BlobfsInspector> inspector;
  CreateBlobfsInspector(std::move(handler), &inspector);
  uint64_t start_index = 12;
  // Test inodes in multiple blocks.
  uint64_t inode_count = 2 * kBlobfsInodesPerBlock;
  uint64_t end_index = start_index + inode_count;

  // Sanity check that nothing is allocated at the start.
  auto inspect_result = inspector->InspectInodeRange(0, end_index);
  ASSERT_TRUE(inspect_result.is_ok());
  std::vector<Inode> initial_inodes = std::move(inspect_result.value());
  ASSERT_EQ(initial_inodes.size(), end_index);

  for (uint64_t i = 0; i < end_index; ++i) {
    ASSERT_FALSE(initial_inodes[i].header.IsAllocated());
  }

  // Actual write.
  std::vector<Inode> write_inodes = AlternateAddInodesAndExtentContainers(inode_count);
  auto write_result = inspector->WriteInodes(write_inodes, start_index);
  ASSERT_TRUE(write_result.is_ok());

  // Test reading back the written inodes.
  auto final_inspect_result = inspector->InspectInodeRange(0, start_index + inode_count);
  ASSERT_TRUE(final_inspect_result.is_ok());
  std::vector<Inode> final_inodes = std::move(final_inspect_result.value());
  ASSERT_EQ(final_inodes.size(), end_index);

  for (uint64_t i = 0; i < start_index; ++i) {
    EXPECT_FALSE(final_inodes[i].header.IsAllocated());
  }

  for (uint64_t i = 0; i < inode_count; ++i) {
    Inode inode = final_inodes[start_index + i];
    EXPECT_TRUE(inode.header.IsAllocated());
    if (i % 2 == 0) {
      ASSERT_TRUE(inode.header.IsInode());
      EXPECT_EQ(inode.block_count, i);
    } else {
      ASSERT_TRUE(inode.header.IsExtentContainer());
      EXPECT_EQ(inode.AsExtentContainer()->previous_node, i);
    }
  }
}

TEST(BlobfsInspector, WriteJournalSuperblock) {
  std::unique_ptr<FakeTransactionHandler> handler;
  CreateFakeBlobfsHandler(&handler);
  std::unique_ptr<BlobfsInspector> inspector;
  CreateBlobfsInspector(std::move(handler), &inspector);

  uint64_t magic = 1234;
  uint64_t start_block = 42;

  // Make sure superblock original values are correct.
  auto inspect_result = inspector->InspectJournalSuperblock();
  ASSERT_TRUE(inspect_result.is_ok());
  fs::JournalInfo journal_info = std::move(inspect_result.value());
  ASSERT_EQ(fs::kJournalMagic, journal_info.magic);
  ASSERT_EQ(0ul, journal_info.start_block);

  fs::JournalInfo new_journal_info = {};
  new_journal_info.magic = magic;
  new_journal_info.start_block = start_block;
  auto write_result = inspector->WriteJournalSuperblock(new_journal_info);
  ASSERT_TRUE(write_result.is_ok());

  // Re-inspect that values have changed.
  inspect_result = inspector->InspectJournalSuperblock();
  ASSERT_TRUE(inspect_result.is_ok());
  journal_info = std::move(inspect_result.value());
  ASSERT_EQ(magic, journal_info.magic);
  ASSERT_EQ(start_block, journal_info.start_block);
}

TEST(BlobfsInspector, WriteJournalEntryBlocks) {
  std::unique_ptr<FakeTransactionHandler> handler;
  CreateFakeBlobfsHandler(&handler);
  std::unique_ptr<BlobfsInspector> inspector;
  CreateBlobfsInspector(std::move(handler), &inspector);

  uint64_t start_index = 0;
  uint64_t payload_blocks = 2;

  // Make sure original values are zero.
  // First four entry blocks should be header, payload, payload, commit.
  fs::JournalHeaderBlock header;
  LoadAndUnwrapJournalEntry(inspector.get(), start_index, &header);
  EXPECT_EQ(0ul, header.prefix.magic);

  fs::JournalPrefix prefix;
  LoadAndUnwrapJournalEntry(inspector.get(), start_index + 1, &prefix);
  EXPECT_EQ(0ul, prefix.magic);

  LoadAndUnwrapJournalEntry(inspector.get(), start_index + 2, &prefix);
  EXPECT_EQ(0ul, prefix.magic);

  fs::JournalCommitBlock commit;
  LoadAndUnwrapJournalEntry(inspector.get(), start_index + 3, &commit);
  EXPECT_EQ(0ul, commit.prefix.magic);

  LoadAndUnwrapJournalEntry(inspector.get(), start_index + 4, &commit);
  EXPECT_EQ(0ul, commit.prefix.magic);

  auto buffer_result = inspector->GetBufferFactory()->CreateBuffer(4);
  ASSERT_TRUE(buffer_result.is_ok());
  std::unique_ptr<storage::BlockBuffer> buffer = buffer_result.take_value();

  // Write header to backing buffer.
  auto header_ptr = reinterpret_cast<fs::JournalHeaderBlock*>(buffer->Data(0));
  header_ptr->prefix.magic = fs::kJournalEntryMagic;
  header_ptr->prefix.flags = fs::kJournalPrefixFlagHeader;
  header_ptr->payload_blocks = payload_blocks;

  // Write commit to backing buffer.
  auto commit_ptr = reinterpret_cast<fs::JournalCommitBlock*>(buffer->Data(3));
  commit_ptr->prefix.magic = fs::kJournalEntryMagic;
  commit_ptr->prefix.flags = fs::kJournalPrefixFlagCommit;

  auto write_result = inspector->WriteJournalEntryBlocks(buffer.get(), start_index);
  ASSERT_TRUE(write_result.is_ok());

  // Re-read written blocks and the block after to make sure it has not changed.
  LoadAndUnwrapJournalEntry(inspector.get(), start_index, &header);
  EXPECT_EQ(header.prefix.magic, fs::kJournalEntryMagic);
  EXPECT_EQ(header.prefix.flags, fs::kJournalPrefixFlagHeader);
  EXPECT_EQ(payload_blocks, header.payload_blocks);

  LoadAndUnwrapJournalEntry(inspector.get(), start_index + 1, &prefix);
  EXPECT_EQ(0ul, prefix.magic);

  LoadAndUnwrapJournalEntry(inspector.get(), start_index + 2, &prefix);
  EXPECT_EQ(0ul, prefix.magic);

  LoadAndUnwrapJournalEntry(inspector.get(), start_index + 3, &commit);
  EXPECT_EQ(fs::kJournalEntryMagic, commit.prefix.magic);
  EXPECT_EQ(fs::kJournalPrefixFlagCommit, commit.prefix.flags);

  LoadAndUnwrapJournalEntry(inspector.get(), start_index + 4, &prefix);
  EXPECT_EQ(0ul, prefix.magic);
}

TEST(BlobfsInspector, WriteBlockAllocationBits) {
  std::unique_ptr<FakeTransactionHandler> handler;
  CreateFakeBlobfsHandler(&handler);
  std::unique_ptr<BlobfsInspector> inspector;
  CreateBlobfsInspector(std::move(handler), &inspector);

  uint64_t start_index = 25;
  uint64_t bits_to_sample = 100;
  uint64_t end_index = start_index + bits_to_sample;
  uint64_t bits_to_write = 50;

  // Make sure original values are not set.
  {
    auto inspect_result = inspector->InspectDataBlockAllocatedInRange(start_index, end_index);
    ASSERT_TRUE(inspect_result.is_ok());
    // No values should be set.
    ASSERT_EQ(0ul, std::move(inspect_result.value()).size());
  }

  // Write bits.
  {
    auto write_result =
        inspector->WriteDataBlockAllocationBits(true, start_index, start_index + bits_to_write);
    ASSERT_TRUE(write_result.is_ok());
  }

  // Re-inspect for changes.
  {
    auto inspect_result = inspector->InspectDataBlockAllocatedInRange(start_index, end_index);
    ASSERT_TRUE(inspect_result.is_ok());
    std::vector<uint64_t> allocated_indices = std::move(inspect_result.value());
    ASSERT_EQ(bits_to_write, allocated_indices.size());

    for (uint64_t i = 0; i < allocated_indices.size(); ++i) {
      EXPECT_EQ(start_index + i, allocated_indices[i]);
    }
  }
}

TEST(BlobfsInspector, WriteDataBlocks) {
  std::unique_ptr<FakeTransactionHandler> handler;
  CreateFakeBlobfsHandler(&handler);

  Superblock superblock =
      *reinterpret_cast<Superblock*>(handler->GetDeviceBuffer()->Data(kSuperblockOffset));
  uint64_t start_offset = 25;
  uint64_t blocks_to_write = 10;
  storage::BlockBuffer* device = handler->GetDeviceBuffer();

  std::unique_ptr<BlobfsInspector> inspector;
  CreateBlobfsInspector(std::move(handler), &inspector);

  auto buffer_result = inspector->GetBufferFactory()->CreateBuffer(blocks_to_write);
  ASSERT_TRUE(buffer_result.is_ok());
  std::unique_ptr<storage::BlockBuffer> buffer = buffer_result.take_value();

  memset(buffer->Data(0), 0xab, blocks_to_write * kBlobfsBlockSize);

  auto write_result = inspector->WriteDataBlocks(buffer.get(), start_offset);
  ASSERT_TRUE(write_result.is_ok());

  EXPECT_EQ(memcmp(buffer->Data(0), device->Data(DataStartBlock(superblock) + start_offset),
                   blocks_to_write * kBlobfsBlockSize),
            0);
}

}  // namespace
}  // namespace blobfs
