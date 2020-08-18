// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "blobfs-checker.h"

#include <lib/sync/completion.h>

#include <blobfs/common.h>
#include <blobfs/format.h>
#include <blobfs/mkfs.h>
#include <block-client/cpp/fake-device.h>
#include <gtest/gtest.h>

#include "blob.h"
#include "blobfs.h"
#include "test/blob_utils.h"
#include "utils.h"

namespace blobfs {
namespace {

using block_client::FakeBlockDevice;

constexpr uint32_t kBlockSize = 512;
constexpr uint32_t kNumBlocks = 400 * kBlobfsBlockSize / kBlockSize;

// Expose access to ReloadSuperblock(). This allows tests to alter the
// Superblock on disk and force blobfs to reload it before running a check.
class TestBlobfs : public Blobfs {
 public:
  zx_status_t Reload() { return ReloadSuperblock(); }
};

class BlobfsCheckerTest : public testing::Test {
 public:
  void SetUp() override {
    auto device = std::make_unique<FakeBlockDevice>(kNumBlocks, kBlockSize);
    ASSERT_TRUE(device);
    ASSERT_EQ(FormatFilesystem(device.get()), ZX_OK);
    loop_.StartThread();

    MountOptions options;
    options.pager = enable_paging;
    ASSERT_EQ(Blobfs::Create(loop_.dispatcher(), std::move(device), &options, zx::resource(), &fs_),
              ZX_OK);
    srand(testing::UnitTest::GetInstance()->random_seed());
  }

  // UpdateSuperblock writes the provided superblock to the block device and
  // forces blobfs to reload immediately.
  zx_status_t UpdateSuperblock(Superblock& superblock) {
    size_t superblock_size = kBlobfsBlockSize * SuperblockBlocks(superblock);
    DeviceBlockWrite(fs_->Device(), &superblock, superblock_size, kSuperblockOffset);
    return static_cast<TestBlobfs*>(fs_.get())->Reload();
  }

  // Sync waits for blobfs to sync with the underlying block device.
  zx_status_t Sync() {
    sync_completion_t completion;
    fs_->Sync([&completion](zx_status_t status) { sync_completion_signal(&completion); });
    return sync_completion_wait(&completion, zx::duration::infinite().get());
  }

  // AddRandomBlob creates and writes a random blob to the file system as a child
  // of the provided Vnode. Optionally returns the block the blob starts at if block_out is
  // provided, and the size of the blob if size_out is provided.
  void AddRandomBlob(fs::Vnode* node, uint64_t* block_out = nullptr, uint64_t* size_out = nullptr) {
    std::unique_ptr<BlobInfo> info;
    GenerateRandomBlob("", 1024, &info);
    memmove(info->path, info->path + 1, strlen(info->path));  // Remove leading slash.

    fbl::RefPtr<fs::Vnode> file;
    ASSERT_EQ(node->Create(&file, info->path, 0), ZX_OK);

    size_t actual;
    EXPECT_EQ(file->Truncate(info->size_data), ZX_OK);
    EXPECT_EQ(file->Write(info->data.get(), info->size_data, 0, &actual), ZX_OK);
    EXPECT_EQ(actual, info->size_data);
    EXPECT_EQ(file->Close(), ZX_OK);

    if (block_out) {
      auto blob = fbl::RefPtr<Blob>::Downcast(file);
      // Get the block that contains the blob.
      *block_out = fs_->GetNode(blob->Ino())->extents[0].Start() + DataStartBlock(fs_->Info());
    }
    if (size_out) {
      *size_out = info->size_data;
    }
  }

  // Creates and writes a corrupt blob to the file system as a child of the provided Vnode.
  void AddCorruptBlob(fs::Vnode* node) {
    uint64_t block, size;
    AddRandomBlob(node, &block, &size);

    // Unmount.
    std::unique_ptr<block_client::BlockDevice> device = Blobfs::Destroy(std::move(fs_));

    // Read the block that contains the blob.
    storage::VmoBuffer buffer;
    ASSERT_EQ(buffer.Initialize(device.get(), 1, kBlobfsBlockSize, "test_buffer"), ZX_OK);
    block_fifo_request_t request = {
        .opcode = BLOCKIO_READ,
        .vmoid = buffer.vmoid(),
        .length = kBlobfsBlockSize / kBlockSize,
        .vmo_offset = 0,
        .dev_offset = block * kBlobfsBlockSize / kBlockSize,
    };
    ASSERT_EQ(device->FifoTransaction(&request, 1), ZX_OK);

    // Flip a random bit of the data.
    auto blob_data = static_cast<uint8_t*>(buffer.Data(0));
    size_t rand_index = rand() % size;
    uint8_t old_val = blob_data[rand_index];
    while ((blob_data[rand_index] = static_cast<uint8_t>(rand())) == old_val) {
    }

    // Write the block back.
    request.opcode = BLOCKIO_WRITE;
    ASSERT_EQ(device->FifoTransaction(&request, 1), ZX_OK);

    MountOptions options;
    options.pager = enable_paging;
    // Remount.
    ASSERT_EQ(Blobfs::Create(loop_.dispatcher(), std::move(device), &options, zx::resource(), &fs_),
              ZX_OK);
  }

  std::unique_ptr<Blobfs> get_fs_unique() { return std::move(fs_); }
  Blobfs* get_fs() { return fs_.get(); }

 protected:
  bool enable_paging = false;

 private:
  async::Loop loop_{&kAsyncLoopConfigNoAttachToCurrentThread};
  std::unique_ptr<Blobfs> fs_;
};

class BlobfsCheckerPagedTest : public BlobfsCheckerTest {
 public:
  void SetUp() {
    enable_paging = true;
    BlobfsCheckerTest::SetUp();
  }
};

void RunTestEmpty(BlobfsCheckerTest* t) {
  BlobfsChecker checker(t->get_fs_unique());
  ASSERT_EQ(checker.Check(), ZX_OK);
}

TEST_F(BlobfsCheckerTest, TestEmpty) { RunTestEmpty(this); }

TEST_F(BlobfsCheckerPagedTest, TestEmpty) { RunTestEmpty(this); }

void RunTestNonEmpty(BlobfsCheckerTest* t) {
  fbl::RefPtr<fs::Vnode> root;
  ASSERT_EQ(t->get_fs()->OpenRootNode(&root), ZX_OK);
  fs::Vnode* root_node = root.get();
  for (unsigned i = 0; i < 3; i++) {
    t->AddRandomBlob(root_node);
  }
  EXPECT_EQ(t->Sync(), ZX_OK);

  BlobfsChecker checker(t->get_fs_unique());
  ASSERT_EQ(checker.Check(), ZX_OK);
}

TEST_F(BlobfsCheckerTest, TestNonEmpty) { RunTestNonEmpty(this); }

TEST_F(BlobfsCheckerPagedTest, TestNonEmpty) { RunTestNonEmpty(this); }

void RunTestInodeWithUnallocatedBlock(BlobfsCheckerTest* t) {
  fbl::RefPtr<fs::Vnode> root;
  ASSERT_EQ(t->get_fs()->OpenRootNode(&root), ZX_OK);
  fs::Vnode* root_node = root.get();
  for (unsigned i = 0; i < 3; i++) {
    t->AddRandomBlob(root_node);
  }
  EXPECT_EQ(t->Sync(), ZX_OK);

  Extent e(1, 1);
  t->get_fs()->GetAllocator()->FreeBlocks(e);

  BlobfsChecker checker(t->get_fs_unique());
  ASSERT_EQ(checker.Check(), ZX_ERR_BAD_STATE);
}

TEST_F(BlobfsCheckerTest, TestInodeWithUnallocatedBlock) { RunTestInodeWithUnallocatedBlock(this); }

TEST_F(BlobfsCheckerPagedTest, TestInodeWithUnallocatedBlock) {
  RunTestInodeWithUnallocatedBlock(this);
}

// TODO(https://bugs.fuchsia.dev/45924): determine why running this test on an
// empty blobfs fails on ASAN QEMU bot.
void RunTestAllocatedBlockCountTooHigh(BlobfsCheckerTest* t) {
  fbl::RefPtr<fs::Vnode> root;
  ASSERT_EQ(t->get_fs()->OpenRootNode(&root), ZX_OK);
  t->AddRandomBlob(root.get());
  EXPECT_EQ(t->Sync(), ZX_OK);

  Superblock superblock = t->get_fs()->Info();
  superblock.alloc_block_count++;
  ASSERT_EQ(t->UpdateSuperblock(superblock), ZX_OK);

  BlobfsChecker checker(t->get_fs_unique());
  ASSERT_EQ(checker.Check(), ZX_ERR_BAD_STATE);
}

TEST_F(BlobfsCheckerTest, TestAllocatedBlockCountTooHigh) {
  RunTestAllocatedBlockCountTooHigh(this);
}

TEST_F(BlobfsCheckerPagedTest, TestAllocatedBlockCountTooHigh) {
  RunTestAllocatedBlockCountTooHigh(this);
}

void RunTestAllocatedBlockCountTooLow(BlobfsCheckerTest* t) {
  fbl::RefPtr<fs::Vnode> root;
  ASSERT_EQ(t->get_fs()->OpenRootNode(&root), ZX_OK);
  fs::Vnode* root_node = root.get();
  for (unsigned i = 0; i < 3; i++) {
    t->AddRandomBlob(root_node);
  }
  EXPECT_EQ(t->Sync(), ZX_OK);

  Superblock superblock = t->get_fs()->Info();
  superblock.alloc_block_count = 2;
  t->UpdateSuperblock(superblock);

  BlobfsChecker checker(t->get_fs_unique());
  ASSERT_EQ(checker.Check(), ZX_ERR_BAD_STATE);
}

TEST_F(BlobfsCheckerTest, TestAllocatedBlockCountTooLow) { RunTestAllocatedBlockCountTooLow(this); }

TEST_F(BlobfsCheckerPagedTest, TestAllocatedBlockCountTooLow) {
  RunTestAllocatedBlockCountTooLow(this);
}

void RunTestFewerThanMinimumBlocksAllocated(BlobfsCheckerTest* t) {
  Extent e(0, 1);
  t->get_fs()->GetAllocator()->FreeBlocks(e);
  BlobfsChecker checker(t->get_fs_unique());
  ASSERT_EQ(checker.Check(), ZX_ERR_BAD_STATE);
}

TEST_F(BlobfsCheckerTest, TestFewerThanMinimumBlocksAllocated) {
  RunTestFewerThanMinimumBlocksAllocated(this);
}

TEST_F(BlobfsCheckerPagedTest, TestFewerThanMinimumBlocksAllocated) {
  RunTestFewerThanMinimumBlocksAllocated(this);
}

void RunTestAllocatedInodeCountTooHigh(BlobfsCheckerTest* t) {
  fbl::RefPtr<fs::Vnode> root;
  ASSERT_EQ(t->get_fs()->OpenRootNode(&root), ZX_OK);
  t->AddRandomBlob(root.get());
  EXPECT_EQ(t->Sync(), ZX_OK);

  Superblock superblock = t->get_fs()->Info();
  superblock.alloc_inode_count++;
  t->UpdateSuperblock(superblock);

  BlobfsChecker checker(t->get_fs_unique());
  ASSERT_EQ(checker.Check(), ZX_ERR_BAD_STATE);
}

TEST_F(BlobfsCheckerTest, TestAllocatedInodeCountTooHigh) {
  RunTestAllocatedInodeCountTooHigh(this);
}

TEST_F(BlobfsCheckerPagedTest, TestAllocatedInodeCountTooHigh) {
  RunTestAllocatedInodeCountTooHigh(this);
}

void RunTestAllocatedInodeCountTooLow(BlobfsCheckerTest* t) {
  fbl::RefPtr<fs::Vnode> root;
  ASSERT_EQ(t->get_fs()->OpenRootNode(&root), ZX_OK);
  fs::Vnode* root_node = root.get();
  for (unsigned i = 0; i < 3; i++) {
    t->AddRandomBlob(root_node);
  }
  EXPECT_EQ(t->Sync(), ZX_OK);

  Superblock superblock = t->get_fs()->Info();
  superblock.alloc_inode_count = 2;
  t->UpdateSuperblock(superblock);

  BlobfsChecker checker(t->get_fs_unique());
  ASSERT_EQ(checker.Check(), ZX_ERR_BAD_STATE);
}

TEST_F(BlobfsCheckerTest, TestAllocatedInodeCountTooLow) { RunTestAllocatedInodeCountTooLow(this); }

TEST_F(BlobfsCheckerPagedTest, TestAllocatedInodeCountTooLow) {
  RunTestAllocatedInodeCountTooLow(this);
}

void RunTestCorruptBlobs(BlobfsCheckerTest* t) {
  fbl::RefPtr<fs::Vnode> root;
  for (unsigned i = 0; i < 5; i++) {
    // Need to get the root node inside the loop because adding a corrupt blob causes us to change
    // the Blobfs instance. The only feasible way right now to corrupt a blob *after* it has been
    // written out involves unmounting and then remounting the file system.
    ASSERT_EQ(t->get_fs()->OpenRootNode(&root), ZX_OK);
    fs::Vnode* root_node = root.get();
    if (i % 2 == 0) {
      t->AddRandomBlob(root_node);
    } else {
      t->AddCorruptBlob(root_node);
    }
  }
  EXPECT_EQ(t->Sync(), ZX_OK);

  BlobfsChecker checker(t->get_fs_unique());
  ASSERT_EQ(checker.Check(), ZX_ERR_BAD_STATE);
}

TEST_F(BlobfsCheckerTest, TestCorruptBlobs) { RunTestCorruptBlobs(this); }

TEST_F(BlobfsCheckerPagedTest, TestCorruptBlobs) { RunTestCorruptBlobs(this); }

}  // namespace
}  // namespace blobfs
