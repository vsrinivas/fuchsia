// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "blobfs/blobfs_inspector.h"

#include <iostream>
#include <memory>

#include <blobfs/common.h>
#include <blobfs/format.h>
#include <buffer/test_support/array_buffer.h>
#include <disk_inspector/buffer_factory.h>
#include <fs/journal/format.h>
#include <fs/journal/initializer.h>
#include <fs/transaction/block_transaction.h>
#include <zxtest/zxtest.h>

namespace blobfs {
namespace {

constexpr uint64_t kBlockCount = 1 << 15;

class FakeTransactionHandler : public fs::TransactionHandler {
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

  groupid_t BlockGroupID() final { return 0; }

  uint32_t DeviceBlockSize() const final { return fake_device_->BlockSize(); }

  block_client::BlockDevice* GetDevice() final { return nullptr; }

  zx_status_t Transaction(block_fifo_request_t* requests, size_t count) final {
    return ZX_ERR_NOT_SUPPORTED;
  }

  void ValidateOperation(const storage::Operation& operation, storage::BlockBuffer* buffer) {
    ASSERT_NOT_NULL(fake_device_);
    ASSERT_GE(buffer->capacity(), operation.vmo_offset + operation.length,
              "Operation goes past input buffer length\n");
    ASSERT_GE(fake_device_->capacity(), operation.dev_offset + operation.length,
              "Operation goes past device buffer length\n");

    ASSERT_NE(operation.type, storage::OperationType::kTrim, "Trim operation is not supported\n");
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
  ASSERT_OK(block_bitmap.Reset(BlockMapBlocks(superblock) * kBlobfsBlockBits));
  block_bitmap.Set(0, kStartBlockMinimum);
  uint64_t bitmap_length = BlockMapBlocks(superblock) * kBlobfsBlockSize;
  memcpy(device->Data(BlockMapStartBlock(superblock)), GetRawBitmapData(block_bitmap, 0),
         bitmap_length);

  // Node map.
  uint64_t nodemap_length = NodeMapBlocks(superblock) * kBlobfsBlockSize;
  memset(device->Data(NodeMapStartBlock(superblock)), 0, nodemap_length);

  // Journal.
  fs::WriteBlockFn write_block_fn = [&device, &superblock](fbl::Span<const uint8_t> buffer,
                                                           uint64_t block_offset) {
    ZX_ASSERT(block_offset < JournalBlocks(superblock));
    ZX_ASSERT(buffer.size() == kBlobfsBlockSize);
    memcpy(device->Data(JournalStartBlock(superblock) + block_offset), buffer.data(),
           kBlobfsBlockSize);
    return ZX_OK;
  };
  ASSERT_OK(fs::MakeJournal(JournalBlocks(superblock), write_block_fn));

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
  *out = result.take_value();
}

TEST(BlobfsInspector, CreateWithoutError) {
  ASSERT_NO_DEATH(
      []() {
        std::unique_ptr<FakeTransactionHandler> handler;
        CreateFakeBlobfsHandler(&handler);
        std::unique_ptr<BlobfsInspector> inspector;
        CreateBlobfsInspector(std::move(handler), &inspector);
      },
      "Could not initialize Blobfs inspector.");
}

TEST(BlobfsInspector, CreateWithoutErrorOnBadSuperblock) {
  ASSERT_NO_DEATH(
      []() {
        std::unique_ptr<FakeTransactionHandler> handler;
        CreateBadFakeBlobfsHandler(&handler);
        std::unique_ptr<BlobfsInspector> inspector;
        CreateBlobfsInspector(std::move(handler), &inspector);
      },
      "Could not initialize Blobfs inspector with bad superblock.");
}

TEST(BlobfsInspector, InspectSuperblock) {
  std::unique_ptr<FakeTransactionHandler> handler;
  CreateFakeBlobfsHandler(&handler);
  std::unique_ptr<BlobfsInspector> inspector;
  CreateBlobfsInspector(std::move(handler), &inspector);

  Superblock sb = inspector->InspectSuperblock();

  EXPECT_EQ(sb.magic0, kBlobfsMagic0);
  EXPECT_EQ(sb.magic1, kBlobfsMagic1);
  EXPECT_EQ(sb.version, kBlobfsVersion);
  EXPECT_EQ(sb.flags, kBlobFlagClean);
  EXPECT_EQ(sb.block_size, kBlobfsBlockSize);
  EXPECT_EQ(sb.alloc_block_count, 1);
  EXPECT_EQ(sb.alloc_inode_count, 0);
  EXPECT_EQ(sb.blob_header_next, 0);
}

TEST(BlobfsInspector, GetInodeCount) {
  std::unique_ptr<FakeTransactionHandler> handler;
  CreateFakeBlobfsHandler(&handler);
  std::unique_ptr<BlobfsInspector> inspector;
  CreateBlobfsInspector(std::move(handler), &inspector);

  Superblock sb = inspector->InspectSuperblock();
  EXPECT_EQ(inspector->GetInodeCount(), sb.inode_count);
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
  ASSERT_EQ(sb.alloc_inode_count, 2);

  auto result = inspector->InspectInodeRange(0, 3);
  ASSERT_TRUE(result.is_ok());
  std::vector<Inode> inodes = result.take_value();
  EXPECT_TRUE(inodes[0].header.IsAllocated());
  EXPECT_TRUE(inodes[0].header.IsInode());
  EXPECT_EQ(inodes[0].block_count, 5);
  EXPECT_EQ(inodes[0].extent_count, 42);

  EXPECT_TRUE(inodes[1].header.IsAllocated());
  EXPECT_FALSE(inodes[1].header.IsInode());
  EXPECT_EQ(inodes[1].AsExtentContainer()->previous_node, 10);
  EXPECT_EQ(inodes[1].AsExtentContainer()->extent_count, 123);

  EXPECT_FALSE(inodes[2].header.IsAllocated());
}

TEST(BlobfsInspector, InspectJournalSuperblock) {
  std::unique_ptr<FakeTransactionHandler> handler;
  CreateFakeBlobfsHandler(&handler);
  std::unique_ptr<BlobfsInspector> inspector;
  CreateBlobfsInspector(std::move(handler), &inspector);

  auto result = inspector->InspectJournalSuperblock();
  ASSERT_TRUE(result.is_ok());
  fs::JournalInfo journal_info = result.take_value();

  EXPECT_EQ(journal_info.magic, fs::kJournalMagic);
  EXPECT_EQ(journal_info.start_block, 0);
}

TEST(BlobfsInspector, GetJournalEntryCount) {
  std::unique_ptr<FakeTransactionHandler> handler;
  CreateFakeBlobfsHandler(&handler);
  std::unique_ptr<BlobfsInspector> inspector;
  CreateBlobfsInspector(std::move(handler), &inspector);

  Superblock sb = inspector->InspectSuperblock();
  uint64_t expected_count = JournalBlocks(sb) - fs::kJournalMetadataBlocks;
  EXPECT_EQ(inspector->GetJournalEntryCount(), expected_count);
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
  EXPECT_EQ(inspector->GetJournalEntryCount(), 0);
}

template <typename T>
void LoadAndUnwrapJournalEntry(BlobfsInspector* inspector, uint64_t index, T* out_value) {
  auto result = inspector->InspectJournalEntryAs<T>(index);
  ASSERT_TRUE(result.is_ok());
  *out_value = result.take_value();
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
  EXPECT_EQ(header.prefix.magic, fs::kJournalEntryMagic);
  EXPECT_EQ(header.prefix.sequence_number, 0);
  EXPECT_EQ(header.prefix.flags, fs::kJournalPrefixFlagHeader);
  EXPECT_EQ(header.payload_blocks, 2);

  fs::JournalPrefix prefix;
  LoadAndUnwrapJournalEntry(inspector.get(), 1, &prefix);
  EXPECT_NE(prefix.magic, fs::kJournalEntryMagic);

  LoadAndUnwrapJournalEntry(inspector.get(), 2, &prefix);
  EXPECT_NE(prefix.magic, fs::kJournalEntryMagic);

  fs::JournalCommitBlock commit;
  LoadAndUnwrapJournalEntry(inspector.get(), 3, &commit);
  EXPECT_EQ(commit.prefix.magic, fs::kJournalEntryMagic);
  EXPECT_EQ(commit.prefix.sequence_number, 0);
  EXPECT_EQ(commit.prefix.flags, fs::kJournalPrefixFlagCommit);
}

TEST(BlobfsInspector, WriteSuperblock) {
  std::unique_ptr<FakeTransactionHandler> handler;
  CreateFakeBlobfsHandler(&handler);
  std::unique_ptr<BlobfsInspector> inspector;
  CreateBlobfsInspector(std::move(handler), &inspector);

  Superblock sb = inspector->InspectSuperblock();
  // Test original values are correct.
  EXPECT_EQ(sb.magic0, kBlobfsMagic0);
  EXPECT_EQ(sb.magic1, kBlobfsMagic1);
  EXPECT_EQ(sb.version, kBlobfsVersion);

  // Edit values and write.
  sb.magic0 = 0;
  sb.version = 0;
  auto result = inspector->WriteSuperblock(sb);
  ASSERT_TRUE(result.is_ok());

  // Test if superblock is saved in memory.
  Superblock edit_sb = inspector->InspectSuperblock();
  EXPECT_EQ(edit_sb.magic0, 0);
  EXPECT_EQ(edit_sb.magic1, kBlobfsMagic1);
  EXPECT_EQ(edit_sb.version, 0);

  // Test reloading from disk.
  ASSERT_OK(inspector->ReloadSuperblock());
  Superblock reload_sb = inspector->InspectSuperblock();
  EXPECT_EQ(reload_sb.magic0, 0);
  EXPECT_EQ(reload_sb.magic1, kBlobfsMagic1);
  EXPECT_EQ(reload_sb.version, 0);
}

}  // namespace
}  // namespace blobfs
