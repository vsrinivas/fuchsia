// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "blobfs-checker.h"

#include <lib/sync/completion.h>

#include <blobfs/common.h>
#include <blobfs/format.h>
#include <blobfs/mkfs.h>
#include <block-client/cpp/fake-device.h>
#include <zxtest/zxtest.h>

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

class BlobfsCheckerTest : public zxtest::Test {
 public:
  void SetUp() final {
    auto device = std::make_unique<FakeBlockDevice>(kNumBlocks, kBlockSize);
    ASSERT_TRUE(device);
    ASSERT_OK(FormatFilesystem(device.get()));
    loop_.StartThread();

    MountOptions options;
    ASSERT_OK(Blobfs::Create(loop_.dispatcher(), std::move(device), &options, &fs_));
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

 protected:
  async::Loop loop_{&kAsyncLoopConfigNoAttachToCurrentThread};
  std::unique_ptr<Blobfs> fs_;
};

// AddRandomBlob creates and writes a random blob to the file system as a child
// of the provided Vnode.
void AddRandomBlob(fs::Vnode* node) {
  std::unique_ptr<BlobInfo> info;
  ASSERT_TRUE(GenerateRandomBlob("", 1024, &info));
  memmove(info->path, info->path + 1, strlen(info->path));  // Remove leading slash.

  fbl::RefPtr<fs::Vnode> file;
  ASSERT_OK(node->Create(&file, info->path, 0));

  size_t actual;
  EXPECT_OK(file->Truncate(info->size_data));
  EXPECT_OK(file->Write(info->data.get(), info->size_data, 0, &actual));
  EXPECT_EQ(actual, info->size_data);
  EXPECT_OK(file->Close());
}

TEST_F(BlobfsCheckerTest, TestEmpty) {
  BlobfsChecker checker(std::move(fs_));
  ASSERT_OK(checker.Check());
}

TEST_F(BlobfsCheckerTest, TestNonEmpty) {
  fbl::RefPtr<fs::Vnode> root;
  ASSERT_OK(fs_->OpenRootNode(&root));
  fs::Vnode* root_node = root.get();
  for (unsigned i = 0; i < 3; i++) {
    AddRandomBlob(root_node);
  }
  EXPECT_OK(Sync());

  BlobfsChecker checker(std::move(fs_));
  ASSERT_OK(checker.Check());
}

TEST_F(BlobfsCheckerTest, TestInodeWithUnallocatedBlock) {
  fbl::RefPtr<fs::Vnode> root;
  ASSERT_OK(fs_->OpenRootNode(&root));
  fs::Vnode* root_node = root.get();
  for (unsigned i = 0; i < 3; i++) {
    AddRandomBlob(root_node);
  }
  EXPECT_OK(Sync());

  Extent e(1, 1);
  fs_->GetAllocator()->FreeBlocks(e);

  BlobfsChecker checker(std::move(fs_));
  ASSERT_STATUS(checker.Check(), ZX_ERR_BAD_STATE);
}

// TODO(https://bugs.fuchsia.dev/45924): determine why running this test on an
// empty blobfs fails on ASAN QEMU bot.
TEST_F(BlobfsCheckerTest, TestAllocatedBlockCountTooHigh) {
  fbl::RefPtr<fs::Vnode> root;
  ASSERT_OK(fs_->OpenRootNode(&root));
  AddRandomBlob(root.get());
  EXPECT_OK(Sync());

  Superblock superblock = fs_->Info();
  superblock.alloc_block_count++;
  ASSERT_OK(UpdateSuperblock(superblock));

  BlobfsChecker checker(std::move(fs_));
  ASSERT_STATUS(checker.Check(), ZX_ERR_BAD_STATE);
}

TEST_F(BlobfsCheckerTest, TestAllocatedBlockCountTooLow) {
  fbl::RefPtr<fs::Vnode> root;
  ASSERT_OK(fs_->OpenRootNode(&root));
  fs::Vnode* root_node = root.get();
  for (unsigned i = 0; i < 3; i++) {
    AddRandomBlob(root_node);
  }
  EXPECT_OK(Sync());

  Superblock superblock = fs_->Info();
  superblock.alloc_block_count = 2;
  UpdateSuperblock(superblock);

  BlobfsChecker checker(std::move(fs_));
  ASSERT_STATUS(checker.Check(), ZX_ERR_BAD_STATE);
}

TEST_F(BlobfsCheckerTest, TestFewerThanMinimumBlocksAllocated) {
  Extent e(0, 1);
  fs_->GetAllocator()->FreeBlocks(e);
  BlobfsChecker checker(std::move(fs_));
  ASSERT_STATUS(checker.Check(), ZX_ERR_BAD_STATE);
}

TEST_F(BlobfsCheckerTest, TestAllocatedInodeCountTooHigh) {
  fbl::RefPtr<fs::Vnode> root;
  ASSERT_OK(fs_->OpenRootNode(&root));
  AddRandomBlob(root.get());
  EXPECT_OK(Sync());

  Superblock superblock = fs_->Info();
  superblock.alloc_inode_count++;
  UpdateSuperblock(superblock);

  BlobfsChecker checker(std::move(fs_));
  ASSERT_STATUS(checker.Check(), ZX_ERR_BAD_STATE);
}

TEST_F(BlobfsCheckerTest, TestAllocatedInodeCountTooLow) {
  fbl::RefPtr<fs::Vnode> root;
  ASSERT_OK(fs_->OpenRootNode(&root));
  fs::Vnode* root_node = root.get();
  for (unsigned i = 0; i < 3; i++) {
    AddRandomBlob(root_node);
  }
  EXPECT_OK(Sync());

  Superblock superblock = fs_->Info();
  superblock.alloc_inode_count = 2;
  UpdateSuperblock(superblock);

  BlobfsChecker checker(std::move(fs_));
  ASSERT_STATUS(checker.Check(), ZX_ERR_BAD_STATE);
}

}  // namespace
}  // namespace blobfs
