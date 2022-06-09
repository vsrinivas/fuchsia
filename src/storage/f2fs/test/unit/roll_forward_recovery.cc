// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstdint>
#include <string_view>
#include <unordered_set>

#include <safemath/checked_math.h>

#include "src/lib/storage/block_client/cpp/fake_block_device.h"
#include "src/storage/f2fs/f2fs.h"
#include "unit_lib.h"

namespace f2fs {
namespace {

zx_status_t CheckDataPage(F2fs *fs, pgoff_t data_blkaddr, uint32_t index) {
  zx_status_t ret = ZX_ERR_INVALID_ARGS;
  LockedPage page;
  if (ret = fs->GetMetaPage(data_blkaddr, &page); ret != ZX_OK) {
    return ret;
  }
  if (*static_cast<uint32_t *>(page->GetAddress()) == index) {
    ret = ZX_OK;
  }
  return ret;
}

zx::status<pgoff_t> CheckNodePage(F2fs *fs, NodePage &node_page) {
  pgoff_t block_count = 0, start_index = 0, checked = 0;

  if (IsInode(node_page)) {
    block_count = kAddrsPerInode;
  } else {
    block_count = kAddrsPerBlock;
  }
  start_index = node_page.StartBidxOfNode();

  for (pgoff_t index = 0; index < block_count; ++index) {
    block_t data_blkaddr = DatablockAddr(&node_page, index);
    if (data_blkaddr == kNullAddr) {
      break;
    }
    if (CheckDataPage(fs, data_blkaddr, safemath::checked_cast<uint32_t>(start_index + index)) ==
        ZX_OK) {
      ++checked;
    } else {
      return zx::error(ZX_ERR_INVALID_ARGS);
    }
  }
  return zx::ok(checked);
}

zx::status<fbl::RefPtr<VnodeF2fs>> CreateFileAndWritePages(Dir *dir_vnode,
                                                           std::string_view file_name,
                                                           pgoff_t page_count, uint32_t signiture) {
  fbl::RefPtr<fs::Vnode> file_fs_vnode;
  if (zx_status_t ret = dir_vnode->Create(file_name, S_IFREG, &file_fs_vnode); ret != ZX_OK) {
    return zx::error(ret);
  }
  fbl::RefPtr<VnodeF2fs> fsync_vnode = fbl::RefPtr<VnodeF2fs>::Downcast(std::move(file_fs_vnode));
  File *fsync_file_ptr = static_cast<File *>(fsync_vnode.get());

  // Write a page
  for (uint32_t index = 0; index < page_count; ++index) {
    uint32_t write_buf[PAGE_SIZE / (sizeof(uint32_t) / sizeof(uint8_t))];
    for (uint32_t &integer : write_buf) {
      integer = index + signiture;
    }
    FileTester::AppendToFile(fsync_file_ptr, write_buf, PAGE_SIZE);
  }
  return zx::ok(std::move(fsync_vnode));
}

void CheckFsyncedFile(F2fs *fs, ino_t ino, pgoff_t data_page_count, pgoff_t node_page_count) {
  block_t data_blkaddr = fs->GetSegmentManager().NextFreeBlkAddr(CursegType::kCursegWarmNode);
  uint64_t curr_checkpoint_ver = fs->GetSuperblockInfo().GetCheckpoint().checkpoint_ver;
  pgoff_t checked_data_page_count = 0;
  pgoff_t checked_node_page_count = 0;

  while (true) {
    LockedPage page;
    ASSERT_EQ(fs->GetMetaPage(data_blkaddr, &page), ZX_OK);
    NodePage *node_page = &page.GetPage<NodePage>();

    if (curr_checkpoint_ver != node_page->CpverOfNode()) {
      break;
    }
    ASSERT_TRUE(node_page->IsFsyncDnode());

    if (IsInode(*page)) {
      ASSERT_EQ(node_page->NidOfNode(), node_page->InoOfNode());
      ASSERT_EQ(node_page->InoOfNode(), ino);
      ASSERT_TRUE(node_page->IsDentDnode());
    }
    auto result = CheckNodePage(fs, *node_page);
    ASSERT_EQ(result.status_value(), ZX_OK);
    data_blkaddr = node_page->NextBlkaddrOfNode();
    checked_data_page_count += result.value();
    ++checked_node_page_count;
  }
  ASSERT_EQ(checked_data_page_count, data_page_count);
  ASSERT_EQ(checked_node_page_count, node_page_count);
}

TEST(FsyncRecoveryTest, FsyncInode) {
  std::unique_ptr<Bcache> bc;
  FileTester::MkfsOnFakeDev(&bc);

  std::unique_ptr<F2fs> fs;
  MountOptions options{};
  // Enable roll-forward recovery
  ASSERT_EQ(options.SetValue(options.GetNameView(kOptDisableRollForward), 0), ZX_OK);
  // Disable inline data option
  ASSERT_EQ(options.SetValue(options.GetNameView(kOptInlineData), 0), ZX_OK);
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  FileTester::MountWithOptions(loop.dispatcher(), options, &bc, &fs);

  fbl::RefPtr<VnodeF2fs> root;
  FileTester::CreateRoot(fs.get(), &root);
  fbl::RefPtr<Dir> root_dir = fbl::RefPtr<Dir>::Downcast(std::move(root));

  // 1. Create file and write data pages
  const pgoff_t data_page_count = 1;
  const pgoff_t node_page_count = 1;
  auto ret = CreateFileAndWritePages(root_dir.get(), "fsync_inode_file", data_page_count, 0);
  ASSERT_TRUE(ret.is_ok());
  auto fsync_vnode = std::move(ret.value());

  // 2. Fsync file
  ino_t fsync_file_ino = fsync_vnode->Ino();
  block_t pre_next_node_blkaddr =
      fs->GetSegmentManager().NextFreeBlkAddr(CursegType::kCursegWarmNode);
  block_t pre_next_data_blkaddr =
      fs->GetSegmentManager().NextFreeBlkAddr(CursegType::kCursegWarmData);

  uint64_t pre_checkpoint_ver = fs->GetSuperblockInfo().GetCheckpoint().checkpoint_ver;
  ASSERT_EQ(fsync_vnode->SyncFile(0, safemath::checked_cast<loff_t>(fsync_vnode->GetSize()), 0),
            ZX_OK);
  uint64_t curr_checkpoint_ver = fs->GetSuperblockInfo().GetCheckpoint().checkpoint_ver;
  // Checkpoint should not be performed instead of fsync
  ASSERT_EQ(pre_checkpoint_ver, curr_checkpoint_ver);

  ASSERT_EQ(fsync_vnode->Close(), ZX_OK);
  fsync_vnode = nullptr;
  ASSERT_EQ(root_dir->Close(), ZX_OK);
  root_dir = nullptr;

  // 3. SPO
  FileTester::SuddenPowerOff(std::move(fs), &bc);

  // 4. Remount without roll-forward recovery
  ASSERT_EQ(options.SetValue(options.GetNameView(kOptDisableRollForward), 1), ZX_OK);
  FileTester::MountWithOptions(loop.dispatcher(), options, &bc, &fs);
  curr_checkpoint_ver = fs->GetSuperblockInfo().GetCheckpoint().checkpoint_ver;
  ASSERT_EQ(pre_checkpoint_ver, curr_checkpoint_ver);

  // 5. Check fsynced inode pages
  block_t curr_next_node_blkaddr =
      fs->GetSegmentManager().NextFreeBlkAddr(CursegType::kCursegWarmNode);
  ASSERT_EQ(pre_next_node_blkaddr, curr_next_node_blkaddr);
  block_t curr_next_data_blkaddr =
      fs->GetSegmentManager().NextFreeBlkAddr(CursegType::kCursegWarmData);
  ASSERT_EQ(pre_next_data_blkaddr, curr_next_data_blkaddr);

  CheckFsyncedFile(fs.get(), fsync_file_ino, data_page_count, node_page_count);

  // 6. Unmount and check filesystem
  FileTester::Unmount(std::move(fs), &bc);
  EXPECT_EQ(Fsck(std::move(bc), FsckOptions{.repair = false}, &bc), ZX_OK);
}

TEST(FsyncRecoveryTest, FsyncDnode) {
  std::unique_ptr<Bcache> bc;
  FileTester::MkfsOnFakeDev(&bc);

  std::unique_ptr<F2fs> fs;
  MountOptions options{};
  // Enable roll-forward recovery
  ASSERT_EQ(options.SetValue(options.GetNameView(kOptDisableRollForward), 0), ZX_OK);
  // Disable inline data option
  ASSERT_EQ(options.SetValue(options.GetNameView(kOptInlineData), 0), ZX_OK);
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  FileTester::MountWithOptions(loop.dispatcher(), options, &bc, &fs);

  fbl::RefPtr<VnodeF2fs> root;
  FileTester::CreateRoot(fs.get(), &root);
  fbl::RefPtr<Dir> root_dir = fbl::RefPtr<Dir>::Downcast(std::move(root));

  // 1. Create file and write data pages to use dnode.
  const pgoff_t data_page_count = kAddrsPerInode + 1;
  const pgoff_t node_page_count = 2;
  auto ret = CreateFileAndWritePages(root_dir.get(), "fsync_dnode_file", data_page_count, 0);
  ASSERT_TRUE(ret.is_ok());
  auto fsync_vnode = std::move(ret.value());

  // 2. Fsync file
  ino_t fsync_file_ino = fsync_vnode->Ino();
  block_t pre_next_node_blkaddr =
      fs->GetSegmentManager().NextFreeBlkAddr(CursegType::kCursegWarmNode);
  block_t pre_next_data_blkaddr =
      fs->GetSegmentManager().NextFreeBlkAddr(CursegType::kCursegWarmData);

  uint64_t pre_checkpoint_ver = fs->GetSuperblockInfo().GetCheckpoint().checkpoint_ver;
  ASSERT_EQ(fsync_vnode->SyncFile(0, safemath::checked_cast<loff_t>(fsync_vnode->GetSize()), 0),
            ZX_OK);
  uint64_t curr_checkpoint_ver = fs->GetSuperblockInfo().GetCheckpoint().checkpoint_ver;
  // Checkpoint should not be performed instead of fsync
  ASSERT_EQ(pre_checkpoint_ver, curr_checkpoint_ver);

  ASSERT_EQ(fsync_vnode->Close(), ZX_OK);
  fsync_vnode = nullptr;
  ASSERT_EQ(root_dir->Close(), ZX_OK);
  root_dir = nullptr;

  // 3. SPO
  FileTester::SuddenPowerOff(std::move(fs), &bc);

  // 4. Remount without roll-forward recovery
  ASSERT_EQ(options.SetValue(options.GetNameView(kOptDisableRollForward), 1), ZX_OK);
  FileTester::MountWithOptions(loop.dispatcher(), options, &bc, &fs);
  curr_checkpoint_ver = fs->GetSuperblockInfo().GetCheckpoint().checkpoint_ver;
  ASSERT_EQ(pre_checkpoint_ver, curr_checkpoint_ver);

  // 5. Check fsynced inode pages
  block_t curr_next_node_blkaddr =
      fs->GetSegmentManager().NextFreeBlkAddr(CursegType::kCursegWarmNode);
  ASSERT_EQ(pre_next_node_blkaddr, curr_next_node_blkaddr);
  block_t curr_next_data_blkaddr =
      fs->GetSegmentManager().NextFreeBlkAddr(CursegType::kCursegWarmData);
  ASSERT_EQ(pre_next_data_blkaddr, curr_next_data_blkaddr);

  CheckFsyncedFile(fs.get(), fsync_file_ino, data_page_count, node_page_count);

  // 6. Unmount and check filesystem
  FileTester::Unmount(std::move(fs), &bc);
  EXPECT_EQ(Fsck(std::move(bc), FsckOptions{.repair = false}, &bc), ZX_OK);
}

TEST(FsyncRecoveryTest, FsyncIndirectDnode) {
  std::unique_ptr<Bcache> bc;
  FileTester::MkfsOnFakeDev(&bc);

  std::unique_ptr<F2fs> fs;
  MountOptions options{};
  // Enable roll-forward recovery
  ASSERT_EQ(options.SetValue(options.GetNameView(kOptDisableRollForward), 0), ZX_OK);
  // Disable inline data option
  ASSERT_EQ(options.SetValue(options.GetNameView(kOptInlineData), 0), ZX_OK);
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  FileTester::MountWithOptions(loop.dispatcher(), options, &bc, &fs);

  fbl::RefPtr<VnodeF2fs> root;
  FileTester::CreateRoot(fs.get(), &root);
  fbl::RefPtr<Dir> root_dir = fbl::RefPtr<Dir>::Downcast(std::move(root));

  // 1. Create file and write data pages to use indirect dnode.
  const pgoff_t data_page_count = kAddrsPerInode + kAddrsPerBlock * 2 + 1;
  const pgoff_t node_page_count = 4;
  auto ret =
      CreateFileAndWritePages(root_dir.get(), "fsync_indirect_dnode_file", data_page_count, 0);
  ASSERT_TRUE(ret.is_ok());
  auto fsync_vnode = std::move(ret.value());

  // 2. Fsync file
  ino_t fsync_file_ino = fsync_vnode->Ino();
  block_t pre_next_node_blkaddr =
      fs->GetSegmentManager().NextFreeBlkAddr(CursegType::kCursegWarmNode);
  block_t pre_next_data_blkaddr =
      fs->GetSegmentManager().NextFreeBlkAddr(CursegType::kCursegWarmData);

  uint64_t pre_checkpoint_ver = fs->GetSuperblockInfo().GetCheckpoint().checkpoint_ver;
  ASSERT_EQ(fsync_vnode->SyncFile(0, safemath::checked_cast<loff_t>(fsync_vnode->GetSize()), 0),
            ZX_OK);
  uint64_t curr_checkpoint_ver = fs->GetSuperblockInfo().GetCheckpoint().checkpoint_ver;
  // Checkpoint should not be performed instead of fsync
  ASSERT_EQ(pre_checkpoint_ver, curr_checkpoint_ver);

  ASSERT_EQ(fsync_vnode->Close(), ZX_OK);
  fsync_vnode = nullptr;
  ASSERT_EQ(root_dir->Close(), ZX_OK);
  root_dir = nullptr;

  // 3. SPO
  FileTester::SuddenPowerOff(std::move(fs), &bc);

  // 4. Remount without roll-forward recovery
  ASSERT_EQ(options.SetValue(options.GetNameView(kOptDisableRollForward), 1), ZX_OK);
  FileTester::MountWithOptions(loop.dispatcher(), options, &bc, &fs);
  curr_checkpoint_ver = fs->GetSuperblockInfo().GetCheckpoint().checkpoint_ver;
  ASSERT_EQ(pre_checkpoint_ver, curr_checkpoint_ver);

  // 5. Check fsynced inode pages
  block_t curr_next_node_blkaddr =
      fs->GetSegmentManager().NextFreeBlkAddr(CursegType::kCursegWarmNode);
  ASSERT_EQ(pre_next_node_blkaddr, curr_next_node_blkaddr);
  block_t curr_next_data_blkaddr =
      fs->GetSegmentManager().NextFreeBlkAddr(CursegType::kCursegWarmData);
  ASSERT_EQ(pre_next_data_blkaddr, curr_next_data_blkaddr);

  CheckFsyncedFile(fs.get(), fsync_file_ino, data_page_count, node_page_count);

  // 6. Unmount and check filesystem
  FileTester::Unmount(std::move(fs), &bc);
  EXPECT_EQ(Fsck(std::move(bc), FsckOptions{.repair = false}, &bc), ZX_OK);
}

TEST(FsyncRecoveryTest, FsyncCheckpoint) {
  std::unique_ptr<Bcache> bc;
  FileTester::MkfsOnFakeDev(&bc);

  std::unique_ptr<F2fs> fs;
  MountOptions options{};
  // Enable roll-forward recovery
  ASSERT_EQ(options.SetValue(options.GetNameView(kOptDisableRollForward), 0), ZX_OK);
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  FileTester::MountWithOptions(loop.dispatcher(), options, &bc, &fs);

  fbl::RefPtr<VnodeF2fs> root;
  FileTester::CreateRoot(fs.get(), &root);
  fbl::RefPtr<Dir> root_dir = fbl::RefPtr<Dir>::Downcast(std::move(root));

  // 1. Fsync directory
  fbl::RefPtr<fs::Vnode> file_fs_vnode;
  ASSERT_EQ(root_dir->Create("fsync_dir", S_IFDIR, &file_fs_vnode), ZX_OK);
  fbl::RefPtr<VnodeF2fs> fsync_vnode = fbl::RefPtr<VnodeF2fs>::Downcast(std::move(file_fs_vnode));

  uint64_t pre_checkpoint_ver = fs->GetSuperblockInfo().GetCheckpoint().checkpoint_ver;
  ASSERT_EQ(fsync_vnode->SyncFile(0, safemath::checked_cast<loff_t>(fsync_vnode->GetSize()), 0),
            ZX_OK);
  uint64_t curr_checkpoint_ver = fs->GetSuperblockInfo().GetCheckpoint().checkpoint_ver;
  // Checkpoint should be performed instead of fsync
  ASSERT_EQ(pre_checkpoint_ver + 1, curr_checkpoint_ver);

  ASSERT_EQ(fsync_vnode->Close(), ZX_OK);
  fsync_vnode = nullptr;

  // 2. Fsync Nlink = 0
  ASSERT_EQ(root_dir->Create("fsync_file_nlink", S_IFREG, &file_fs_vnode), ZX_OK);
  fsync_vnode = fbl::RefPtr<VnodeF2fs>::Downcast(std::move(file_fs_vnode));
  uint32_t temp_nlink = fsync_vnode->GetNlink();
  fsync_vnode->ClearNlink();

  pre_checkpoint_ver = fs->GetSuperblockInfo().GetCheckpoint().checkpoint_ver;
  ASSERT_EQ(fsync_vnode->SyncFile(0, safemath::checked_cast<loff_t>(fsync_vnode->GetSize()), 0),
            ZX_OK);
  curr_checkpoint_ver = fs->GetSuperblockInfo().GetCheckpoint().checkpoint_ver;
  // Checkpoint should be performed instead of fsync
  ASSERT_EQ(pre_checkpoint_ver + 1, curr_checkpoint_ver);
  fsync_vnode->SetNlink(temp_nlink);
  fsync_vnode->MarkInodeDirty();
  fsync_vnode->WriteInode();

  ASSERT_EQ(fsync_vnode->Close(), ZX_OK);
  fsync_vnode = nullptr;

  // 3. Fsync vnode with kNeedCp flag
  ASSERT_EQ(root_dir->Create("fsync_file_need_cp", S_IFREG, &file_fs_vnode), ZX_OK);
  fsync_vnode = fbl::RefPtr<VnodeF2fs>::Downcast(std::move(file_fs_vnode));
  fsync_vnode->SetFlag(InodeInfoFlag::kNeedCp);

  pre_checkpoint_ver = fs->GetSuperblockInfo().GetCheckpoint().checkpoint_ver;
  ASSERT_EQ(fsync_vnode->SyncFile(0, safemath::checked_cast<loff_t>(fsync_vnode->GetSize()), 0),
            ZX_OK);
  curr_checkpoint_ver = fs->GetSuperblockInfo().GetCheckpoint().checkpoint_ver;
  // Checkpoint should be performed instead of fsync
  ASSERT_EQ(pre_checkpoint_ver + 1, curr_checkpoint_ver);

  ASSERT_EQ(fsync_vnode->Close(), ZX_OK);
  fsync_vnode = nullptr;

  // 4. Not enough SpaceForRollForward
  ASSERT_EQ(root_dir->Create("fsync_file_space_for_roll_forward", S_IFREG, &file_fs_vnode), ZX_OK);
  fsync_vnode = fbl::RefPtr<VnodeF2fs>::Downcast(std::move(file_fs_vnode));
  block_t temp_user_block_count = fs->GetSuperblockInfo().GetUserBlockCount();
  fs->GetSuperblockInfo().SetUserBlockCount(0);

  pre_checkpoint_ver = fs->GetSuperblockInfo().GetCheckpoint().checkpoint_ver;
  ASSERT_EQ(fsync_vnode->SyncFile(0, safemath::checked_cast<loff_t>(fsync_vnode->GetSize()), 0),
            ZX_OK);
  curr_checkpoint_ver = fs->GetSuperblockInfo().GetCheckpoint().checkpoint_ver;
  // Checkpoint should be performed instead of fsync
  ASSERT_EQ(pre_checkpoint_ver + 1, curr_checkpoint_ver);
  fs->GetSuperblockInfo().SetUserBlockCount(temp_user_block_count);

  ASSERT_EQ(fsync_vnode->Close(), ZX_OK);
  fsync_vnode = nullptr;

  // 5. NeedToSyncDir()
  FileTester::CreateChild(root_dir.get(), S_IFDIR, "parent_dir");
  fbl::RefPtr<fs::Vnode> child_dir_vn;
  FileTester::Lookup(root_dir.get(), "parent_dir", &child_dir_vn);
  ASSERT_EQ(child_dir_vn->Create("fsync_file", S_IFREG, &file_fs_vnode), ZX_OK);
  fsync_vnode = fbl::RefPtr<VnodeF2fs>::Downcast(std::move(file_fs_vnode));

  pre_checkpoint_ver = fs->GetSuperblockInfo().GetCheckpoint().checkpoint_ver;
  ASSERT_EQ(fsync_vnode->SyncFile(0, safemath::checked_cast<loff_t>(fsync_vnode->GetSize()), 0),
            ZX_OK);
  curr_checkpoint_ver = fs->GetSuperblockInfo().GetCheckpoint().checkpoint_ver;
  // Checkpoint should be performed instead of fsync
  ASSERT_EQ(pre_checkpoint_ver + 1, curr_checkpoint_ver);

  ASSERT_EQ(fsync_vnode->Close(), ZX_OK);
  fsync_vnode = nullptr;
  ASSERT_EQ(child_dir_vn->Close(), ZX_OK);
  child_dir_vn = nullptr;
  ASSERT_EQ(root_dir->Close(), ZX_OK);
  root_dir = nullptr;

  // 6. Enable kMountDisableRollForward option
  // Remount without roll-forward recovery
  FileTester::Unmount(std::move(fs), &bc);
  ASSERT_EQ(options.SetValue(options.GetNameView(kOptDisableRollForward), 1), ZX_OK);
  FileTester::MountWithOptions(loop.dispatcher(), options, &bc, &fs);

  FileTester::CreateRoot(fs.get(), &root);
  root_dir = fbl::RefPtr<Dir>::Downcast(std::move(root));
  ASSERT_EQ(root_dir->Create("fsync_file_disable_roll_forward", S_IFREG, &file_fs_vnode), ZX_OK);
  fsync_vnode = fbl::RefPtr<VnodeF2fs>::Downcast(std::move(file_fs_vnode));

  pre_checkpoint_ver = fs->GetSuperblockInfo().GetCheckpoint().checkpoint_ver;
  ASSERT_EQ(fsync_vnode->SyncFile(0, safemath::checked_cast<loff_t>(fsync_vnode->GetSize()), 0),
            ZX_OK);
  curr_checkpoint_ver = fs->GetSuperblockInfo().GetCheckpoint().checkpoint_ver;
  // Checkpoint should be performed instead of fsync
  ASSERT_EQ(pre_checkpoint_ver + 1, curr_checkpoint_ver);

  ASSERT_EQ(fsync_vnode->Close(), ZX_OK);
  fsync_vnode = nullptr;
  ASSERT_EQ(root_dir->Close(), ZX_OK);
  root_dir = nullptr;

  // Unmount and check filesystem
  FileTester::Unmount(std::move(fs), &bc);
  EXPECT_EQ(Fsck(std::move(bc), FsckOptions{.repair = false}, &bc), ZX_OK);
}

TEST(FsyncRecoveryTest, FsyncRecoveryIndirectDnode) {
  std::unique_ptr<Bcache> bc;
  FileTester::MkfsOnFakeDev(&bc);

  std::unique_ptr<F2fs> fs;
  MountOptions options{};
  // Enable roll-forward recovery
  ASSERT_EQ(options.SetValue(options.GetNameView(kOptDisableRollForward), 0), ZX_OK);
  // Disable inline data option
  ASSERT_EQ(options.SetValue(options.GetNameView(kOptInlineData), 0), ZX_OK);
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  FileTester::MountWithOptions(loop.dispatcher(), options, &bc, &fs);

  fbl::RefPtr<VnodeF2fs> root;
  FileTester::CreateRoot(fs.get(), &root);
  fbl::RefPtr<Dir> root_dir = fbl::RefPtr<Dir>::Downcast(std::move(root));

  // 1. Create file and write data pages to use indirect dnode.
  const pgoff_t data_page_count = kAddrsPerInode + kAddrsPerBlock * 2 + 1;
  std::string file_name("recovery_indirect_dnode_file");
  auto ret = CreateFileAndWritePages(root_dir.get(), file_name, data_page_count, 0);
  ASSERT_TRUE(ret.is_ok());
  auto fsync_vnode = std::move(ret.value());

  // 2. Fsync file
  uint64_t pre_checkpoint_ver = fs->GetSuperblockInfo().GetCheckpoint().checkpoint_ver;
  ASSERT_EQ(fsync_vnode->SyncFile(0, safemath::checked_cast<loff_t>(fsync_vnode->GetSize()), 0),
            ZX_OK);
  uint64_t curr_checkpoint_ver = fs->GetSuperblockInfo().GetCheckpoint().checkpoint_ver;
  // Checkpoint should not be performed instead of fsync
  ASSERT_EQ(pre_checkpoint_ver, curr_checkpoint_ver);

  ASSERT_EQ(fsync_vnode->Close(), ZX_OK);
  fsync_vnode = nullptr;
  ASSERT_EQ(root_dir->Close(), ZX_OK);
  root_dir = nullptr;

  // 4. SPO
  FileTester::SuddenPowerOff(std::move(fs), &bc);

  // 5. Remount with roll-forward recovery
  FileTester::MountWithOptions(loop.dispatcher(), options, &bc, &fs);
  curr_checkpoint_ver = fs->GetSuperblockInfo().GetCheckpoint().checkpoint_ver;
  ASSERT_EQ(pre_checkpoint_ver + 1, curr_checkpoint_ver);

  // 6. Check fsynced file
  FileTester::CreateRoot(fs.get(), &root);
  root_dir = fbl::RefPtr<Dir>::Downcast(std::move(root));

  fbl::RefPtr<fs::Vnode> file_fs_vnode;
  FileTester::Lookup(root_dir.get(), file_name, &file_fs_vnode);
  fsync_vnode = fbl::RefPtr<VnodeF2fs>::Downcast(std::move(file_fs_vnode));
  File *fsync_file_ptr = static_cast<File *>(fsync_vnode.get());

  ASSERT_EQ(fsync_vnode->GetSize(), data_page_count * PAGE_SIZE);

  for (uint32_t index = 0; index < data_page_count; ++index) {
    uint32_t write_buf[PAGE_SIZE / (sizeof(uint32_t) / sizeof(uint8_t))];
    FileTester::ReadFromFile(fsync_file_ptr, write_buf, PAGE_SIZE,
                             static_cast<size_t>(index) * PAGE_SIZE);
    ASSERT_EQ(write_buf[0], index);
  }

  ASSERT_EQ(fsync_vnode->Close(), ZX_OK);
  fsync_vnode = nullptr;
  ASSERT_EQ(root_dir->Close(), ZX_OK);
  root_dir = nullptr;

  // 7. Unmount and check filesystem
  FileTester::Unmount(std::move(fs), &bc);
  EXPECT_EQ(Fsck(std::move(bc), FsckOptions{.repair = false}, &bc), ZX_OK);
}

TEST(FsyncRecoveryTest, FsyncRecoveryMultipleFiles) {
  std::unique_ptr<Bcache> bc;
  FileTester::MkfsOnFakeDev(&bc);

  std::unique_ptr<F2fs> fs;
  MountOptions options{};
  // Enable roll-forward recovery
  ASSERT_EQ(options.SetValue(options.GetNameView(kOptDisableRollForward), 0), ZX_OK);
  // Disable inline data option
  ASSERT_EQ(options.SetValue(options.GetNameView(kOptInlineData), 0), ZX_OK);
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  FileTester::MountWithOptions(loop.dispatcher(), options, &bc, &fs);

  fbl::RefPtr<VnodeF2fs> root;
  FileTester::CreateRoot(fs.get(), &root);
  fbl::RefPtr<Dir> root_dir = fbl::RefPtr<Dir>::Downcast(std::move(root));

  // 1. Create file 1
  const pgoff_t data_page_count_1 = kAddrsPerInode + kAddrsPerBlock * 2 + 1;
  uint32_t file_1_signature = 0x111111;
  std::string file_name_1("recovery_file_1");
  auto ret =
      CreateFileAndWritePages(root_dir.get(), file_name_1, data_page_count_1, file_1_signature);
  ASSERT_TRUE(ret.is_ok());
  auto fsync_vnode_1 = std::move(ret.value());

  // 2. Fsync file 1
  uint64_t pre_checkpoint_ver = fs->GetSuperblockInfo().GetCheckpoint().checkpoint_ver;
  ASSERT_EQ(fsync_vnode_1->SyncFile(0, safemath::checked_cast<loff_t>(fsync_vnode_1->GetSize()), 0),
            ZX_OK);
  uint64_t curr_checkpoint_ver = fs->GetSuperblockInfo().GetCheckpoint().checkpoint_ver;
  // Checkpoint should not be performed instead of fsync
  ASSERT_EQ(pre_checkpoint_ver, curr_checkpoint_ver);

  // 3. Create file 2
  const pgoff_t data_page_count_2 = kAddrsPerInode + kAddrsPerBlock * 2 + 1;
  uint32_t file_2_signature = 0x222222;
  std::string file_name_2("recovery_file_2");
  ret = CreateFileAndWritePages(root_dir.get(), file_name_2, data_page_count_2, file_2_signature);
  ASSERT_TRUE(ret.is_ok());
  auto fsync_vnode_2 = std::move(ret.value());

  // 4. Fsync file 2
  pre_checkpoint_ver = fs->GetSuperblockInfo().GetCheckpoint().checkpoint_ver;
  ASSERT_EQ(fsync_vnode_2->SyncFile(0, safemath::checked_cast<loff_t>(fsync_vnode_2->GetSize()), 0),
            ZX_OK);
  curr_checkpoint_ver = fs->GetSuperblockInfo().GetCheckpoint().checkpoint_ver;
  // Checkpoint should not be performed instead of fsync
  ASSERT_EQ(pre_checkpoint_ver, curr_checkpoint_ver);

  ASSERT_EQ(fsync_vnode_1->Close(), ZX_OK);
  fsync_vnode_1 = nullptr;
  ASSERT_EQ(fsync_vnode_2->Close(), ZX_OK);
  fsync_vnode_2 = nullptr;
  ASSERT_EQ(root_dir->Close(), ZX_OK);
  root_dir = nullptr;

  // 5. SPO
  FileTester::SuddenPowerOff(std::move(fs), &bc);

  // 6. Remount with roll-forward recovery
  FileTester::MountWithOptions(loop.dispatcher(), options, &bc, &fs);
  curr_checkpoint_ver = fs->GetSuperblockInfo().GetCheckpoint().checkpoint_ver;
  ASSERT_EQ(pre_checkpoint_ver + 1, curr_checkpoint_ver);

  FileTester::CreateRoot(fs.get(), &root);
  root_dir = fbl::RefPtr<Dir>::Downcast(std::move(root));

  // 7. Check fsynced file 1
  fbl::RefPtr<fs::Vnode> file_fs_vnode_1;
  FileTester::Lookup(root_dir.get(), file_name_1, &file_fs_vnode_1);
  fsync_vnode_1 = fbl::RefPtr<VnodeF2fs>::Downcast(std::move(file_fs_vnode_1));
  File *fsync_file_ptr_1 = static_cast<File *>(fsync_vnode_1.get());

  ASSERT_EQ(fsync_vnode_1->GetSize(), data_page_count_1 * PAGE_SIZE);

  for (uint32_t index = 0; index < data_page_count_1; ++index) {
    uint32_t write_buf[PAGE_SIZE / (sizeof(uint32_t) / sizeof(uint8_t))];
    FileTester::ReadFromFile(fsync_file_ptr_1, write_buf, PAGE_SIZE,
                             static_cast<size_t>(index) * PAGE_SIZE);
    ASSERT_EQ(write_buf[0], index + file_1_signature);
  }

  // 8. Check fsynced file 2
  fbl::RefPtr<fs::Vnode> file_fs_vnode_2;
  FileTester::Lookup(root_dir.get(), file_name_2, &file_fs_vnode_2);
  fsync_vnode_2 = fbl::RefPtr<VnodeF2fs>::Downcast(std::move(file_fs_vnode_2));
  File *fsync_file_ptr_2 = static_cast<File *>(fsync_vnode_2.get());

  ASSERT_EQ(fsync_vnode_2->GetSize(), data_page_count_2 * PAGE_SIZE);

  for (uint32_t index = 0; index < data_page_count_2; ++index) {
    uint32_t write_buf[PAGE_SIZE / (sizeof(uint32_t) / sizeof(uint8_t))];
    FileTester::ReadFromFile(fsync_file_ptr_2, write_buf, PAGE_SIZE,
                             static_cast<size_t>(index) * PAGE_SIZE);
    ASSERT_EQ(write_buf[0], index + file_2_signature);
  }

  ASSERT_EQ(fsync_vnode_1->Close(), ZX_OK);
  fsync_vnode_1 = nullptr;
  ASSERT_EQ(fsync_vnode_2->Close(), ZX_OK);
  fsync_vnode_2 = nullptr;
  ASSERT_EQ(root_dir->Close(), ZX_OK);
  root_dir = nullptr;

  // 9. Unmount and check filesystem
  FileTester::Unmount(std::move(fs), &bc);
  EXPECT_EQ(Fsck(std::move(bc), FsckOptions{.repair = false}, &bc), ZX_OK);
}

TEST(FsyncRecoveryTest, FsyncRecoveryInlineData) {
  srand(testing::UnitTest::GetInstance()->random_seed());

  std::unique_ptr<Bcache> bc;
  FileTester::MkfsOnFakeDev(&bc);

  std::unique_ptr<F2fs> fs;
  MountOptions options{};
  // Enable roll-forward recovery
  ASSERT_EQ(options.SetValue(options.GetNameView(kOptDisableRollForward), 0), ZX_OK);
  // Enable inline data option
  ASSERT_EQ(options.SetValue(options.GetNameView(kOptInlineData), 1), ZX_OK);
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  FileTester::MountWithOptions(loop.dispatcher(), options, &bc, &fs);

  fbl::RefPtr<VnodeF2fs> root;
  FileTester::CreateRoot(fs.get(), &root);
  fbl::RefPtr<Dir> root_dir = fbl::RefPtr<Dir>::Downcast(std::move(root));

  // The inline_data recovery policy is as follows.
  // [prev.] [next] of inline_data flag
  //    o       o  -> 1. recover inline_data
  //    o       x  -> 2. remove inline_data, and then recover data blocks
  //    x       o  -> 3. remove data blocks, and then recover inline_data (TODO)

  // 1. recover inline_data
  // Inline file creation
  std::string inline_file_name("inline");
  fbl::RefPtr<fs::Vnode> inline_raw_vnode;
  ASSERT_EQ(root_dir->Create(inline_file_name, S_IFREG, &inline_raw_vnode), ZX_OK);
  fbl::RefPtr<VnodeF2fs> inline_vnode =
      fbl::RefPtr<VnodeF2fs>::Downcast(std::move(inline_raw_vnode));
  File *inline_file_ptr = static_cast<File *>(inline_vnode.get());
  FileTester::CheckInlineFile(inline_vnode.get());

  fs->WriteCheckpoint(false, false);

  // Write until entire inline data space is written
  size_t target_size = inline_file_ptr->MaxInlineData() - 1;

  char w_buf[inline_file_ptr->MaxInlineData()];
  char r_buf[inline_file_ptr->MaxInlineData()];

  for (size_t i = 0; i < inline_file_ptr->MaxInlineData(); ++i) {
    w_buf[i] = static_cast<char>(rand());
  }

  FileTester::AppendToFile(inline_file_ptr, w_buf, target_size);
  FileTester::CheckInlineFile(inline_vnode.get());
  ASSERT_EQ(inline_file_ptr->GetSize(), target_size);

  ASSERT_EQ(inline_vnode->SyncFile(0, safemath::checked_cast<loff_t>(inline_vnode->GetSize()), 0),
            ZX_OK);

  ASSERT_EQ(inline_vnode->Close(), ZX_OK);
  inline_vnode = nullptr;
  ASSERT_EQ(root_dir->Close(), ZX_OK);
  root_dir = nullptr;

  // SPO and remount with roll-forward recovery
  FileTester::SuddenPowerOff(std::move(fs), &bc);
  FileTester::MountWithOptions(loop.dispatcher(), options, &bc, &fs);

  FileTester::CreateRoot(fs.get(), &root);
  root_dir = fbl::RefPtr<Dir>::Downcast(std::move(root));

  FileTester::Lookup(root_dir.get(), inline_file_name, &inline_raw_vnode);
  inline_vnode = fbl::RefPtr<VnodeF2fs>::Downcast(std::move(inline_raw_vnode));
  inline_file_ptr = static_cast<File *>(inline_vnode.get());
  FileTester::CheckInlineFile(inline_vnode.get());

  // Check recovery inline data
  FileTester::ReadFromFile(inline_file_ptr, r_buf, target_size, 0);
  ASSERT_EQ(memcmp(r_buf, w_buf, target_size), 0);

  // 2. remove inline_data, and then recover data blocks
  // Write one more byte, then it should be converted to noinline
  target_size = inline_file_ptr->MaxInlineData();

  FileTester::CheckInlineFile(inline_vnode.get());
  FileTester::AppendToFile(inline_file_ptr, &(w_buf[target_size - 1]), 1);
  FileTester::CheckNonInlineFile(inline_vnode.get());
  ASSERT_EQ(inline_file_ptr->GetSize(), target_size);

  ASSERT_EQ(inline_vnode->SyncFile(0, safemath::checked_cast<loff_t>(inline_vnode->GetSize()), 0),
            ZX_OK);

  inline_file_ptr = nullptr;
  ASSERT_EQ(inline_vnode->Close(), ZX_OK);
  inline_vnode = nullptr;
  ASSERT_EQ(root_dir->Close(), ZX_OK);
  root_dir = nullptr;

  // SPO and remount with roll-forward recovery
  FileTester::SuddenPowerOff(std::move(fs), &bc);
  FileTester::MountWithOptions(loop.dispatcher(), options, &bc, &fs);

  FileTester::CreateRoot(fs.get(), &root);
  root_dir = fbl::RefPtr<Dir>::Downcast(std::move(root));

  FileTester::Lookup(root_dir.get(), inline_file_name, &inline_raw_vnode);
  inline_vnode = fbl::RefPtr<VnodeF2fs>::Downcast(std::move(inline_raw_vnode));
  inline_file_ptr = static_cast<File *>(inline_vnode.get());
  FileTester::CheckNonInlineFile(inline_vnode.get());

  ASSERT_EQ(inline_file_ptr->GetSize(), target_size);
  FileTester::ReadFromFile(inline_file_ptr, r_buf, target_size, 0);
  ASSERT_EQ(memcmp(r_buf, w_buf, target_size), 0);

  // TODO: We should consider converting data blocks to inline data.
  // 3. remove inline_data, and then recover inline_data
  // Truncate one byte, then it should be converted to inline
  // target_size = inline_file_ptr->MaxInlineData() - 1;

  // inline_vnode->Truncate(target_size);
  // FileTester::CheckInlineFile(inline_vnode.get());
  // ASSERT_EQ(inline_file_ptr->GetSize(), target_size);

  // ASSERT_EQ(inline_vnode->SyncFile(0, safemath::checked_cast<loff_t>(inline_vnode->GetSize()),
  // 0),
  //           ZX_OK);

  // inline_file_ptr = nullptr;
  // ASSERT_EQ(inline_vnode->Close(), ZX_OK);
  // inline_vnode = nullptr;
  // ASSERT_EQ(root_dir->Close(), ZX_OK);
  // root_dir = nullptr;

  // // SPO and remount with roll-forward recovery
  // FileTester::SuddenPowerOff(std::move(fs), &bc);
  // FileTester::MountWithOptions(loop.dispatcher(), options, &bc, &fs);

  // FileTester::CreateRoot(fs.get(), &root);
  // root_dir = fbl::RefPtr<Dir>::Downcast(std::move(root));

  // FileTester::Lookup(root_dir.get(), inline_file_name, &inline_raw_vnode);
  // inline_vnode = fbl::RefPtr<VnodeF2fs>::Downcast(std::move(inline_raw_vnode));
  // inline_file_ptr = static_cast<File *>(inline_vnode.get());
  // FileTester::CheckInlineFile(inline_vnode.get());

  // FileTester::ReadFromFile(inline_file_ptr, r_buf, target_size, 0);
  // ASSERT_EQ(memcmp(r_buf, w_buf, target_size), 0);

  inline_file_ptr = nullptr;
  ASSERT_EQ(inline_vnode->Close(), ZX_OK);
  inline_vnode = nullptr;
  ASSERT_EQ(root_dir->Close(), ZX_OK);
  root_dir = nullptr;

  // Unmount and check filesystem
  FileTester::Unmount(std::move(fs), &bc);
  EXPECT_EQ(Fsck(std::move(bc), FsckOptions{.repair = false}, &bc), ZX_OK);
}

TEST(FsyncRecoveryTest, RecoveryWithoutFsync) {
  std::unique_ptr<Bcache> bc;
  FileTester::MkfsOnFakeDev(&bc);

  std::unique_ptr<F2fs> fs;
  MountOptions options{};
  // Enable roll-forward recovery
  ASSERT_EQ(options.SetValue(options.GetNameView(kOptDisableRollForward), 0), ZX_OK);
  // Disable inline data option
  ASSERT_EQ(options.SetValue(options.GetNameView(kOptInlineData), 0), ZX_OK);
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  FileTester::MountWithOptions(loop.dispatcher(), options, &bc, &fs);

  fbl::RefPtr<VnodeF2fs> root;
  FileTester::CreateRoot(fs.get(), &root);
  fbl::RefPtr<Dir> root_dir = fbl::RefPtr<Dir>::Downcast(std::move(root));

  // 1. Create file and write data pages to use indirect dnode.
  const pgoff_t data_page_count = 1;
  std::string file_name("recovery_without_fsync_file");
  auto ret = CreateFileAndWritePages(root_dir.get(), file_name, data_page_count, 0);

  auto fsync_vnode = std::move(ret.value());

  ASSERT_EQ(fsync_vnode->Close(), ZX_OK);
  fsync_vnode = nullptr;
  ASSERT_EQ(root_dir->Close(), ZX_OK);
  root_dir = nullptr;

  // 2. SPO without fsync
  FileTester::SuddenPowerOff(std::move(fs), &bc);

  // 3. Remount with roll-forward recovery
  FileTester::MountWithOptions(loop.dispatcher(), options, &bc, &fs);

  // 4. Check fsynced file
  FileTester::CreateRoot(fs.get(), &root);
  root_dir = fbl::RefPtr<Dir>::Downcast(std::move(root));

  // File not found.
  fbl::RefPtr<fs::Vnode> file_fs_vnode;
  FileTester::Lookup(root_dir.get(), file_name, &file_fs_vnode);
  ASSERT_EQ(file_fs_vnode, nullptr);

  ASSERT_EQ(root_dir->Close(), ZX_OK);
  root_dir = nullptr;

  // 5. Unmount and check filesystem
  FileTester::Unmount(std::move(fs), &bc);
  EXPECT_EQ(Fsck(std::move(bc), FsckOptions{.repair = false}, &bc), ZX_OK);
}

}  // namespace
}  // namespace f2fs
