// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/f2fs/f2fs.h"

#include <gtest/gtest.h>
#include <safemath/checked_math.h>

#include "src/lib/storage/block_client/cpp/fake_block_device.h"
#include "unit_lib.h"

namespace f2fs {
namespace {

TEST(SuperblockTest, SanityCheckRawSuper) {
  std::unique_ptr<Bcache> bc;
  FileTester::MkfsOnFakeDevWithOptions(&bc, MkfsOptions{});

  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);

  auto superblock = F2fs::LoadSuperblock(*bc);
  ASSERT_TRUE(superblock.is_ok());
  Superblock *superblock_ptr = (*superblock).get();

  // Create a vfs object for unit tests.
  auto vfs_or = Runner::CreateRunner(loop.dispatcher());
  ZX_ASSERT(vfs_or.is_ok());
  std::unique_ptr<F2fs> fs = std::make_unique<F2fs>(
      loop.dispatcher(), std::move(bc), std::move(*superblock), MountOptions{}, (*vfs_or).get());
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

  auto superblock = F2fs::LoadSuperblock(*bc);
  ASSERT_TRUE(superblock.is_ok());
  Superblock *superblock_ptr = (*superblock).get();

  // Create a vfs object for unit tests.
  auto vfs_or = Runner::CreateRunner(loop.dispatcher());
  ZX_ASSERT(vfs_or.is_ok());
  std::unique_ptr<F2fs> fs = std::make_unique<F2fs>(
      loop.dispatcher(), std::move(bc), std::move(*superblock), MountOptions{}, (*vfs_or).get());

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

  auto superblock = F2fs::LoadSuperblock(*bc);
  ASSERT_TRUE(superblock.is_ok());
  Superblock *superblock_ptr = (*superblock).get();

  // Create a vfs object for unit tests.
  auto vfs_or = Runner::CreateRunner(loop.dispatcher());
  ZX_ASSERT(vfs_or.is_ok());
  std::unique_ptr<F2fs> fs = std::make_unique<F2fs>(
      loop.dispatcher(), std::move(bc), std::move(*superblock), MountOptions{}, (*vfs_or).get());

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

  auto superblock = F2fs::LoadSuperblock(*bc);
  ASSERT_TRUE(superblock.is_ok());

  // Create a vfs object for unit tests.
  auto vfs_or = Runner::CreateRunner(loop.dispatcher());
  ZX_ASSERT(vfs_or.is_ok());
  std::unique_ptr<F2fs> fs = std::make_unique<F2fs>(
      loop.dispatcher(), std::move(bc), std::move(*superblock), MountOptions{}, (*vfs_or).get());

  ASSERT_EQ(fs->FillSuper(), ZX_OK);
  fs->GetVCache().Reset();

  ASSERT_TRUE(fs->IsValid());
  fs->ResetGcManager();
  ASSERT_FALSE(fs->IsValid());
  fs->ResetNodeManager();
  ASSERT_FALSE(fs->IsValid());
  fs->ResetSegmentManager();
  ASSERT_FALSE(fs->IsValid());
  fs->ResetSuperblockInfo();
  ASSERT_FALSE(fs->IsValid());
  fs->ResetPsuedoVnodes();
  ASSERT_FALSE(fs->IsValid());
  ASSERT_TRUE(fs->GetRootVnode().is_error());

  ASSERT_EQ(fs->FillSuper(), ZX_OK);
  fs->GetVCache().Reset();

  ASSERT_TRUE(fs->IsValid());
  fs->Reset();
  ASSERT_FALSE(fs->IsValid());
}

TEST(RunnerTest, CreateException) {
  uint64_t block_count = 20ull * 1024ull * 1024ull / kDefaultSectorSize;
  auto device =
      std::make_unique<block_client::FakeBlockDevice>(block_client::FakeBlockDevice::Config{
          .block_count = block_count, .block_size = kDefaultSectorSize, .supports_trim = true});
  auto bc_or = CreateBcache(std::move(device));
  ASSERT_TRUE(bc_or.is_ok());

  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);

  ASSERT_EQ(Runner::Create(loop.dispatcher(), std::move(*bc_or), MountOptions{}).status_value(),
            ZX_ERR_INVALID_ARGS);
}

TEST(RunnerTest, GetRootVnodeException) {
  std::unique_ptr<Bcache> bc;
  FileTester::MkfsOnFakeDev(&bc, 819200, kDefaultSectorSize);
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  auto vfs_or = Runner::Create(loop.dispatcher(), std::move(bc), MountOptions{});
  ASSERT_TRUE(vfs_or.is_ok());
  vfs_or->Shutdown([](zx_status_t status) {});
  loop.RunUntilIdle();
  ASSERT_TRUE(vfs_or->ServeRoot({}).is_error());
}

TEST(F2fsTest, TakeBc) {
  std::unique_ptr<Bcache> bc;
  FileTester::MkfsOnFakeDevWithOptions(&bc, MkfsOptions{});
  Bcache *bcache_ptr = bc.get();

  std::unique_ptr<F2fs> fs;
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  FileTester::MountWithOptions(loop.dispatcher(), MountOptions{}, &bc, &fs);
  ASSERT_TRUE(fs->IsValid());
  ASSERT_EQ(&fs->GetBc(), bcache_ptr);

  fs->PutSuper();
  auto bc_or = fs->TakeBc();
  ASSERT_TRUE(bc_or.is_ok());
  ASSERT_TRUE(fs->TakeBc().is_error());
  ASSERT_FALSE(fs->IsValid());
  fs.reset();
  bc = std::move(*bc_or);
  ASSERT_EQ(bc.get(), bcache_ptr);

  FileTester::MkfsOnFakeDevWithOptions(&bc, {});
  FileTester::MountWithOptions(loop.dispatcher(), MountOptions{}, &bc, &fs);

  fs->PutSuper();
  ASSERT_TRUE(fs->TakeBc().is_ok());
  ASSERT_TRUE(fs->TakeBc().is_error());
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

TEST(F2fsTest, GetFilesystemInfo) {
  std::unique_ptr<Bcache> bc;
  FileTester::MkfsOnFakeDevWithOptions(&bc, MkfsOptions{});

  std::unique_ptr<F2fs> fs;
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  FileTester::MountWithOptions(loop.dispatcher(), MountOptions{}, &bc, &fs);

  auto &sb_info = fs->GetSuperblockInfo();
  auto info_or = fs->GetFilesystemInfo();
  ASSERT_TRUE(info_or.is_ok());
  auto info = info_or.value();

  ASSERT_EQ(info.block_size, kBlockSize);
  ASSERT_EQ(info.max_filename_size, kMaxNameLen);
  ASSERT_EQ(info.fs_type, fuchsia_fs::VfsType::kF2Fs);
  ASSERT_EQ(info.total_bytes, sb_info.GetUserBlockCount() * kBlockSize);
  ASSERT_EQ(info.used_bytes, sb_info.GetTotalValidBlockCount() * kBlockSize);
  ASSERT_EQ(info.total_nodes, sb_info.GetTotalNodeCount());
  ASSERT_EQ(info.used_nodes, sb_info.GetTotalValidInodeCount());
  ASSERT_EQ(info.name, "f2fs");

  // Check type conversion
  block_t tmp_user_block_count = sb_info.GetUserBlockCount();
  block_t tmp_valid_block_count = sb_info.GetUserBlockCount();

  constexpr uint64_t LARGE_BLOCK_COUNT = 26214400;  // 100GB

  sb_info.SetUserBlockCount(LARGE_BLOCK_COUNT);
  sb_info.SetTotalValidBlockCount(LARGE_BLOCK_COUNT);

  info_or = fs->GetFilesystemInfo();
  ASSERT_TRUE(info_or.is_ok());
  info = info_or.value();

  ASSERT_EQ(info.total_bytes, LARGE_BLOCK_COUNT * kBlockSize);
  ASSERT_EQ(info.used_bytes, LARGE_BLOCK_COUNT * kBlockSize);

  sb_info.SetUserBlockCount(tmp_user_block_count);
  sb_info.SetTotalValidBlockCount(tmp_valid_block_count);
  FileTester::Unmount(std::move(fs), &bc);
}

}  // namespace
}  // namespace f2fs
