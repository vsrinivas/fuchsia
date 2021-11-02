// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/f2fs/f2fs.h"

#include <block-client/cpp/fake-device.h>
#include <gtest/gtest.h>
#include <safemath/checked_math.h>

#include "unit_lib.h"

namespace f2fs {
namespace {

TEST(SuperblockTest, LoadSuperblockException) {
  std::unique_ptr<Bcache> bc;
  Superblock superblock;

  auto device =
      std::make_unique<block_client::FakeBlockDevice>(block_client::FakeBlockDevice::Config{
          .block_count = 1, .block_size = kDefaultSectorSize, .supports_trim = true});
  bool readonly_device = false;
  ASSERT_EQ(f2fs::CreateBcache(std::move(device), &readonly_device, &bc), ZX_OK);

  ASSERT_EQ(LoadSuperblock(bc.get(), &superblock), ZX_OK);
  ASSERT_EQ(LoadSuperblock(bc.get(), &superblock, kSuperblockStart + 1), ZX_ERR_OUT_OF_RANGE);
}

TEST(SuperblockTest, SanityCheckRawSuper) {
  std::unique_ptr<Bcache> bc;
  FileTester::MkfsOnFakeDevWithOptions(&bc, MkfsOptions{});

  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);

  auto superblock = std::make_unique<Superblock>();
  Superblock *superblock_ptr = superblock.get();
  ASSERT_EQ(LoadSuperblock(bc.get(), superblock.get()), ZX_OK);

  std::unique_ptr<F2fs> fs = std::make_unique<F2fs>(loop.dispatcher(), std::move(bc),
                                                    std::move(superblock), MountOptions{});

  // Check SanityCheckRawSuper
  ASSERT_EQ(fs->FillSuper(), ZX_OK);

  // Check SanityCheckRawSuper exception case
  superblock_ptr->log_sectors_per_block = kDefaultSectorsPerBlock;
  superblock_ptr->log_sectorsize = kMaxLogSectorSize;
  ASSERT_EQ(fs->FillSuper(), ZX_ERR_INVALID_ARGS);

  superblock_ptr->log_sectorsize = kMaxLogSectorSize + 1;
  ASSERT_EQ(fs->FillSuper(), ZX_ERR_INVALID_ARGS);

  superblock_ptr->log_blocksize = kMaxLogSectorSize + 1;
  ASSERT_EQ(fs->FillSuper(), ZX_ERR_INVALID_ARGS);

  superblock_ptr->magic = 0xF2F5FFFF;
  ASSERT_EQ(fs->FillSuper(), ZX_ERR_INVALID_ARGS);

  fs->GetVCache().Reset();
}

TEST(SuperblockTest, GetValidCheckpoint) {
  std::unique_ptr<Bcache> bc;
  FileTester::MkfsOnFakeDevWithOptions(&bc, MkfsOptions{});

  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);

  auto superblock = std::make_unique<Superblock>();
  Superblock *superblock_ptr = superblock.get();
  ASSERT_EQ(LoadSuperblock(bc.get(), superblock.get()), ZX_OK);

  std::unique_ptr<F2fs> fs = std::make_unique<F2fs>(loop.dispatcher(), std::move(bc),
                                                    std::move(superblock), MountOptions{});

  // Check GetValidCheckpoint
  ASSERT_EQ(fs->FillSuper(), ZX_OK);

  // Check GetValidCheckpoint exception case
  superblock_ptr->cp_blkaddr = LeToCpu(superblock_ptr->cp_blkaddr) + 2;
  ASSERT_EQ(fs->FillSuper(), ZX_ERR_INVALID_ARGS);

  fs->GetVCache().Reset();
}

TEST(SuperblockTest, SanityCheckCkpt) {
  std::unique_ptr<Bcache> bc;
  FileTester::MkfsOnFakeDevWithOptions(&bc, MkfsOptions{});

  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);

  auto superblock = std::make_unique<Superblock>();
  Superblock *superblock_ptr = superblock.get();
  ASSERT_EQ(LoadSuperblock(bc.get(), superblock.get()), ZX_OK);

  std::unique_ptr<F2fs> fs = std::make_unique<F2fs>(loop.dispatcher(), std::move(bc),
                                                    std::move(superblock), MountOptions{});

  // Check SanityCheckCkpt
  ASSERT_EQ(fs->FillSuper(), ZX_OK);

  // Check SanityCheckCkpt exception case
  superblock_ptr->segment_count_nat = 0;
  ASSERT_EQ(fs->FillSuper(), ZX_ERR_BAD_STATE);

  superblock_ptr->segment_count = 0;
  ASSERT_EQ(fs->FillSuper(), ZX_ERR_BAD_STATE);

  fs->GetVCache().Reset();
}

TEST(SuperblockTest, Reset) {
  std::unique_ptr<Bcache> bc;
  FileTester::MkfsOnFakeDevWithOptions(&bc, MkfsOptions{});

  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);

  auto superblock = std::make_unique<Superblock>();
  ASSERT_EQ(LoadSuperblock(bc.get(), superblock.get()), ZX_OK);

  std::unique_ptr<F2fs> fs = std::make_unique<F2fs>(loop.dispatcher(), std::move(bc),
                                                    std::move(superblock), MountOptions{});

  ASSERT_EQ(fs->FillSuper(), ZX_OK);
  fs->GetVCache().Reset();

  ASSERT_TRUE(fs->IsValid());
  fs->ResetNodeManager();
  ASSERT_FALSE(fs->IsValid());
  fs->ResetSegmentManager();
  ASSERT_FALSE(fs->IsValid());
  fs->ResetSuperblockInfo();
  ASSERT_FALSE(fs->IsValid());
  fs->ResetRootVnode();
  ASSERT_FALSE(fs->IsValid());

  ASSERT_EQ(fs->FillSuper(), ZX_OK);
  fs->GetVCache().Reset();

  ASSERT_TRUE(fs->IsValid());
  fs->Reset();
  ASSERT_FALSE(fs->IsValid());
}

TEST(F2fsTest, CreateException) {
  std::unique_ptr<Bcache> bc;
  auto device =
      std::make_unique<block_client::FakeBlockDevice>(block_client::FakeBlockDevice::Config{
          .block_count = 1, .block_size = kDefaultSectorSize, .supports_trim = true});
  bool readonly_device = false;
  ASSERT_EQ(f2fs::CreateBcache(std::move(device), &readonly_device, &bc), ZX_OK);

  std::unique_ptr<F2fs> fs;
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);

  ASSERT_EQ(F2fs::Create(loop.dispatcher(), std::move(bc), MountOptions{}, &fs),
            ZX_ERR_INVALID_ARGS);
}

TEST(F2fsTest, CreateFsAndRootException) {
  std::unique_ptr<Bcache> bc;
  auto device =
      std::make_unique<block_client::FakeBlockDevice>(block_client::FakeBlockDevice::Config{
          .block_count = 1, .block_size = kDefaultSectorSize, .supports_trim = true});
  bool readonly_device = false;
  ASSERT_EQ(f2fs::CreateBcache(std::move(device), &readonly_device, &bc), ZX_OK);

  std::unique_ptr<F2fs> fs;
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);

  zx::channel outgoing_server = zx::channel(zx_take_startup_handle(PA_DIRECTORY_REQUEST));
  zx::channel root_server = zx::channel(zx_take_startup_handle(FS_HANDLE_ROOT_ID));

  fidl::ServerEnd<fuchsia_io::Directory> export_root =
      fidl::ServerEnd<fuchsia_io::Directory>(std::move(outgoing_server));
  f2fs::ServeLayout serve_layout = f2fs::ServeLayout::kExportDirectory;

  auto on_unmount = [&loop]() {
    loop.Quit();
    FX_LOGS(WARNING) << "Unmounted";
  };

  auto fs_or = CreateFsAndRoot(MountOptions{}, loop.dispatcher(), std::move(bc),
                               std::move(export_root), std::move(on_unmount), serve_layout);
  ASSERT_EQ(fs_or.error_value(), ZX_ERR_INVALID_ARGS);
}

TEST(F2fsTest, FlushDirtyMetaPage) {
  std::unique_ptr<Bcache> bc;
  FileTester::MkfsOnFakeDevWithOptions(&bc, MkfsOptions{});

  std::unique_ptr<F2fs> fs;
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);

  FileTester::MountWithOptions(loop.dispatcher(), MountOptions{}, &bc, &fs);

  fs->GetSuperblockInfo().AddPageCount(CountType::kDirtyMeta);

  Checkpoint &checkpoint = fs->GetSuperblockInfo().GetCheckpoint();
  block_t checkpoint_block = fs->GetSuperblockInfo().StartCpAddr();
  Page *checkpoint_page = fs->GrabMetaPage(checkpoint_block);
  void *kaddr = PageAddress(checkpoint_page);
  memcpy(kaddr, &checkpoint, (1 << fs->GetSuperblockInfo().GetLogBlocksize()));

  ASSERT_EQ(FlushDirtyMetaPage(fs.get(), *checkpoint_page), ZX_OK);

  ASSERT_EQ(fs->GetSuperblockInfo().GetPageCount(CountType::kDirtyMeta), 0);

  F2fsPutPage(checkpoint_page, 1);

  FileTester::Unmount(std::move(fs), &bc);
}

TEST(F2fsTest, FlushDirtyNodePage) {
  std::unique_ptr<Bcache> bc;
  FileTester::MkfsOnFakeDevWithOptions(&bc, MkfsOptions{});

  std::unique_ptr<F2fs> fs;
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);

  FileTester::MountWithOptions(loop.dispatcher(), MountOptions{}, &bc, &fs);

  // read the root inode block
  Page *root_node_page = nullptr;
  SuperblockInfo &superblock_info = fs->GetSuperblockInfo();
  fs->GetNodeManager().GetNodePage(superblock_info.GetRootIno(), &root_node_page);
  ASSERT_NE(root_node_page, nullptr);

  ASSERT_EQ(FlushDirtyNodePage(fs.get(), *root_node_page), ZX_OK);

  ASSERT_EQ(fs->GetSuperblockInfo().GetPageCount(CountType::kDirtyNodes), 0);

  F2fsPutPage(root_node_page, 0);
  FileTester::Unmount(std::move(fs), &bc);
}

TEST(F2fsTest, FlushDirtyDataPage) {
  std::unique_ptr<Bcache> bc;
  FileTester::MkfsOnFakeDevWithOptions(&bc, MkfsOptions{});

  std::unique_ptr<F2fs> fs;
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  FileTester::MountWithOptions(loop.dispatcher(), MountOptions{}, &bc, &fs);

  fbl::RefPtr<VnodeF2fs> root;
  FileTester::CreateRoot(fs.get(), &root);
  fbl::RefPtr<Dir> root_dir = fbl::RefPtr<Dir>::Downcast(std::move(root));

  fbl::RefPtr<fs::Vnode> vnode;
  ASSERT_EQ(root_dir->Create("Data", S_IFREG, &vnode), ZX_OK);
  VnodeF2fs *vn = static_cast<VnodeF2fs *>(vnode.get());

  Page *data_page = GrabCachePage(vn, vn->Ino(), 100);

  ASSERT_EQ(FlushDirtyDataPage(fs.get(), *data_page), ZX_OK);

  F2fsPutPage(data_page, 0);

  ASSERT_EQ(vnode->Close(), ZX_OK);
  vnode = nullptr;
  ASSERT_EQ(root_dir->Close(), ZX_OK);
  root_dir = nullptr;

  FileTester::Unmount(std::move(fs), &bc);
}

TEST(F2fsTest, ResetBc) {
  std::unique_ptr<Bcache> bc;
  FileTester::MkfsOnFakeDevWithOptions(&bc, MkfsOptions{});
  Bcache *bcache_ptr = bc.get();

  std::unique_ptr<F2fs> fs;
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  FileTester::MountWithOptions(loop.dispatcher(), MountOptions{}, &bc, &fs);
  ASSERT_TRUE(fs->IsValid());
  ASSERT_EQ(&fs->GetBc(), bcache_ptr);

  fs->PutSuper();
  fs->ResetBc(&bc);
  ASSERT_FALSE(fs->IsValid());
  fs.reset();
  ASSERT_EQ(bc.get(), bcache_ptr);

  FileTester::MkfsOnFakeDevWithOptions(&bc, {});
  FileTester::MountWithOptions(loop.dispatcher(), MountOptions{}, &bc, &fs);

  fs->PutSuper();
  fs->ResetBc();
  ASSERT_FALSE(fs->IsValid());
}

TEST(F2fsTest, FsBlock) {
  FsBlock block;
  uint8_t data[kBlockSize];
  memset(data, 0, kBlockSize);
  ASSERT_EQ(memcmp(block.GetData().data(), data, kBlockSize), 0);

  memset(data, 0xf2, kBlockSize);
  FsBlock data_block(data);
  ASSERT_EQ(memcmp(data_block.GetData().data(), data, kBlockSize), 0);

  memset(data, 0xf5, kBlockSize);
  data_block = data;
  ASSERT_EQ(memcmp(data_block.GetData().data(), data, kBlockSize), 0);
}

}  // namespace
}  // namespace f2fs
