// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/syslog/cpp/macros.h>

#include <algorithm>
#include <cstddef>
#include <numeric>
#include <vector>

#include <gtest/gtest.h>

#include "src/lib/storage/block_client/cpp/fake_block_device.h"
#include "src/storage/f2fs/f2fs.h"
#include "unit_lib.h"

namespace f2fs {
namespace {

constexpr uint32_t kOrphanCnt = 10;

TEST(OrphanInode, RecoverOrphanInode) {
  std::unique_ptr<Bcache> bc;
  FileTester::MkfsOnFakeDev(&bc);

  std::unique_ptr<F2fs> fs;
  MountOptions options{};
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  FileTester::MountWithOptions(loop.dispatcher(), options, &bc, &fs);

  fbl::RefPtr<VnodeF2fs> root;
  FileTester::CreateRoot(fs.get(), &root);
  fbl::RefPtr<Dir> root_dir = fbl::RefPtr<Dir>::Downcast(std::move(root));

  ASSERT_FALSE(fs->GetSuperblockInfo().TestCpFlags(CpFlag::kCpOrphanPresentFlag));

  // 1. Create files
  std::vector<fbl::RefPtr<VnodeF2fs>> vnodes;
  std::vector<uint32_t> inos;

  ASSERT_EQ(fs->ValidInodeCount(), static_cast<uint64_t>(1));
  ASSERT_EQ(fs->ValidNodeCount(), static_cast<uint64_t>(1));
  ASSERT_EQ(fs->ValidUserBlocks(), static_cast<uint64_t>(2));

  FileTester::CreateChildren(fs.get(), vnodes, inos, root_dir, "orphan_", kOrphanCnt);
  ASSERT_EQ(vnodes.size(), kOrphanCnt);
  ASSERT_EQ(inos.size(), kOrphanCnt);

  ASSERT_EQ(fs->ValidInodeCount(), static_cast<uint64_t>(kOrphanCnt + 1));
  ASSERT_EQ(fs->ValidNodeCount(), static_cast<uint64_t>(kOrphanCnt + 1));
  ASSERT_EQ(fs->ValidUserBlocks(), static_cast<uint64_t>(kOrphanCnt + 2));

  for (const auto &iter : vnodes) {
    ASSERT_EQ(iter->GetNlink(), static_cast<uint32_t>(1));
  }

  // 2. Make orphan inodes
  ASSERT_EQ(fs->GetSuperblockInfo().GetVnodeSetSize(InoType::kOrphanIno), static_cast<uint64_t>(0));
  FileTester::DeleteChildren(vnodes, root_dir, kOrphanCnt);
  ASSERT_EQ(fs->GetSuperblockInfo().GetVnodeSetSize(InoType::kOrphanIno), kOrphanCnt);

  for (const auto &iter : vnodes) {
    ASSERT_EQ(iter->GetNlink(), (uint32_t)0);
  }

  fs->WriteCheckpoint(false, true);

  // 3. Sudden power off
  for (const auto &iter : vnodes) {
    iter->Close();
  }

  vnodes.clear();
  vnodes.shrink_to_fit();
  ASSERT_EQ(root_dir->Close(), ZX_OK);
  root_dir.reset();

  FileTester::SuddenPowerOff(std::move(fs), &bc);

  // 4. Remount and recover orphan inodes
  FileTester::MountWithOptions(loop.dispatcher(), options, &bc, &fs);

  ASSERT_EQ(fs->GetSuperblockInfo().GetVnodeSetSize(InoType::kOrphanIno), static_cast<uint64_t>(0));

  ASSERT_EQ(fs->ValidInodeCount(), static_cast<uint64_t>(1));
  ASSERT_EQ(fs->ValidNodeCount(), static_cast<uint64_t>(1));
  ASSERT_EQ(fs->ValidUserBlocks(), static_cast<uint64_t>(2));

  // Check Orphan nids has been freed
  for (const auto &iter : inos) {
    NodeInfoDeprecated ni;
    fs->GetNodeManager().GetNodeInfo(iter, ni);
    ASSERT_EQ(ni.blk_addr, kNullAddr);
  }

  FileTester::Unmount(std::move(fs), &bc);
}

using OrphanTest = F2fsFakeDevTestFixture;

TEST_F(OrphanTest, VnodeSet) {
  SuperblockInfo &superblock_info = fs_->GetSuperblockInfo();

  uint32_t inode_count = 100;
  std::vector<uint32_t> inos(inode_count);
  std::iota(inos.begin(), inos.end(), 0);

  for (auto ino : inos) {
    superblock_info.AddVnodeToVnodeSet(InoType::kOrphanIno, ino);
  }
  ASSERT_EQ(superblock_info.GetVnodeSetSize(InoType::kOrphanIno), inode_count);

  // Duplicate ino insertion
  superblock_info.AddVnodeToVnodeSet(InoType::kOrphanIno, 1);
  superblock_info.AddVnodeToVnodeSet(InoType::kOrphanIno, 2);
  superblock_info.AddVnodeToVnodeSet(InoType::kOrphanIno, 3);
  superblock_info.AddVnodeToVnodeSet(InoType::kOrphanIno, 4);
  ASSERT_EQ(superblock_info.GetVnodeSetSize(InoType::kOrphanIno), inode_count);

  superblock_info.RemoveVnodeFromVnodeSet(InoType::kOrphanIno, 10);
  ASSERT_EQ(superblock_info.GetVnodeSetSize(InoType::kOrphanIno), inode_count - 1);

  ASSERT_FALSE(superblock_info.FindVnodeFromVnodeSet(InoType::kOrphanIno, 10));
  ASSERT_TRUE(superblock_info.FindVnodeFromVnodeSet(InoType::kOrphanIno, 11));
  superblock_info.AddVnodeToVnodeSet(InoType::kOrphanIno, 10);

  std::vector<uint32_t> tmp_inos;
  superblock_info.ForAllVnodesInVnodeSet(InoType::kOrphanIno,
                                         [&tmp_inos](nid_t ino) { tmp_inos.push_back(ino); });
  ASSERT_TRUE(std::equal(inos.begin(), inos.end(), tmp_inos.begin()));

  for (auto ino : inos) {
    superblock_info.RemoveVnodeFromVnodeSet(InoType::kOrphanIno, ino);
  }
}

}  // namespace
}  // namespace f2fs
