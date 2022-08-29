// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <unordered_set>

#include <gtest/gtest.h>
#include <safemath/checked_math.h>

#include "src/lib/storage/block_client/cpp/fake_block_device.h"
#include "src/storage/f2fs/f2fs.h"
#include "unit_lib.h"

namespace f2fs {
namespace {

using SegmentManagerTest = F2fsFakeDevTestFixture;

TEST_F(SegmentManagerTest, BlkChaining) {
  SuperblockInfo &superblock_info = fs_->GetSuperblockInfo();
  std::vector<block_t> blk_chain(0);
  int nwritten = kDefaultBlocksPerSegment * 2;
  // write the root inode, and read the block where the previous version of the root inode is stored
  // to check if the block has a proper lba address to the next node block
  for (int i = 0; i < nwritten; ++i) {
    NodeInfo ni;
    {
      LockedPage read_page;
      fs_->GetNodeManager().GetNodePage(superblock_info.GetRootIno(), &read_page);
      blk_chain.push_back(read_page.GetPage<NodePage>().NextBlkaddrOfNode());
      read_page->SetDirty();
    }
    WritebackOperation op = {.bSync = true};
    fs_->GetNodeVnode().Writeback(op);

    fs_->GetNodeManager().GetNodeInfo(superblock_info.GetRootIno(), ni);
    ASSERT_NE(ni.blk_addr, kNullAddr);
    ASSERT_NE(ni.blk_addr, kNewAddr);
    ASSERT_EQ(ni.blk_addr, blk_chain[i]);
  }
}

TEST_F(SegmentManagerTest, DirtyToFree) {
  SuperblockInfo &superblock_info = fs_->GetSuperblockInfo();

  // check the precond. before making dirty segments
  std::vector<uint32_t> prefree_array(0);
  int nwritten = kDefaultBlocksPerSegment * 2;
  uint32_t nprefree = 0;
  ASSERT_FALSE(fs_->GetSegmentManager().PrefreeSegments());
  uint32_t nfree_segs = fs_->GetSegmentManager().FreeSegments();
  FreeSegmapInfo *free_i = &fs_->GetSegmentManager().GetFreeSegmentInfo();
  DirtySeglistInfo *dirty_i = &fs_->GetSegmentManager().GetDirtySegmentInfo();

  // write the root inode repeatedly as much as 2 segments
  for (int i = 0; i < nwritten; ++i) {
    NodeInfo ni;
    fs_->GetNodeManager().GetNodeInfo(superblock_info.GetRootIno(), ni);
    ASSERT_NE(ni.blk_addr, kNullAddr);
    ASSERT_NE(ni.blk_addr, kNewAddr);
    block_t old_addr = ni.blk_addr;

    {
      LockedPage read_page;
      fs_->GetNodeManager().GetNodePage(superblock_info.GetRootIno(), &read_page);
      read_page->SetDirty();
    }

    WritebackOperation op = {.bSync = true};
    fs_->GetNodeVnode().Writeback(op);

    if (fs_->GetSegmentManager().GetValidBlocks(fs_->GetSegmentManager().GetSegmentNumber(old_addr),
                                                0) == 0) {
      prefree_array.push_back(fs_->GetSegmentManager().GetSegmentNumber(old_addr));
      ASSERT_EQ(fs_->GetSegmentManager().PrefreeSegments(), ++nprefree);
    }
  }

  // check the bitmaps and the number of free/prefree segments
  ASSERT_EQ(fs_->GetSegmentManager().FreeSegments(), nfree_segs - nprefree);
  for (auto &pre : prefree_array) {
    ASSERT_TRUE(TestBit(pre, dirty_i->dirty_segmap[static_cast<int>(DirtyType::kPre)].get()));
    ASSERT_TRUE(TestBit(pre, free_i->free_segmap.get()));
  }
  // triggers checkpoint to make prefree segments transit to free ones
  fs_->WriteCheckpoint(false, false);

  // check the bitmaps and the number of free/prefree segments
  for (auto &pre : prefree_array) {
    ASSERT_FALSE(TestBit(pre, dirty_i->dirty_segmap[static_cast<int>(DirtyType::kPre)].get()));
    ASSERT_FALSE(TestBit(pre, free_i->free_segmap.get()));
  }
  ASSERT_EQ(fs_->GetSegmentManager().FreeSegments(), nfree_segs);
  ASSERT_FALSE(fs_->GetSegmentManager().PrefreeSegments());
}

TEST_F(SegmentManagerTest, BalanceFs) {
  SuperblockInfo &superblock_info = fs_->GetSuperblockInfo();
  uint32_t nfree_segs = fs_->GetSegmentManager().FreeSegments();

  superblock_info.ClearOnRecovery();
  fs_->GetSegmentManager().BalanceFs();

  ASSERT_EQ(fs_->GetSegmentManager().FreeSegments(), nfree_segs);
  ASSERT_FALSE(fs_->GetSegmentManager().PrefreeSegments());

  superblock_info.SetOnRecovery();
  fs_->GetSegmentManager().BalanceFs();

  ASSERT_EQ(fs_->GetSegmentManager().FreeSegments(), nfree_segs);
  ASSERT_FALSE(fs_->GetSegmentManager().PrefreeSegments());
}

TEST_F(SegmentManagerTest, InvalidateBlocksExceptionCase) {
  // read the root inode block
  LockedPage root_node_page;
  SuperblockInfo &superblock_info = fs_->GetSuperblockInfo();
  fs_->GetNodeManager().GetNodePage(superblock_info.GetRootIno(), &root_node_page);
  ASSERT_NE(root_node_page, nullptr);

  // Check InvalidateBlocks() exception case
  block_t temp_written_valid_blocks = fs_->GetSegmentManager().GetSitInfo().written_valid_blocks;
  fs_->GetSegmentManager().InvalidateBlocks(kNewAddr);
  ASSERT_EQ(temp_written_valid_blocks, fs_->GetSegmentManager().GetSitInfo().written_valid_blocks);
}

TEST_F(SegmentManagerTest, GetNewSegmentHeap) {
  SuperblockInfo &superblock_info = fs_->GetSuperblockInfo();

  // Check GetNewSegment() on AllocDirection::kAllocLeft
  superblock_info.ClearOpt(kMountNoheap);
  uint32_t nwritten = kDefaultBlocksPerSegment * 3;

  for (uint32_t i = 0; i < nwritten; ++i) {
    NodeInfo ni, new_ni;
    fs_->GetNodeManager().GetNodeInfo(superblock_info.GetRootIno(), ni);
    ASSERT_NE(ni.blk_addr, kNullAddr);
    ASSERT_NE(ni.blk_addr, kNewAddr);

    {
      LockedPage read_page;
      fs_->GetNodeManager().GetNodePage(superblock_info.GetRootIno(), &read_page);
      read_page->SetDirty();
    }
    WritebackOperation op = {.bSync = true};
    fs_->GetNodeVnode().Writeback(op);

    fs_->GetNodeManager().GetNodeInfo(superblock_info.GetRootIno(), new_ni);
    ASSERT_NE(new_ni.blk_addr, kNullAddr);
    ASSERT_NE(new_ni.blk_addr, kNewAddr);

    // first segment already has next segment with noheap option
    if ((i > kDefaultBlocksPerSegment - 1) && ((ni.blk_addr + 1) % kDefaultBlocksPerSegment == 0)) {
      ASSERT_LT(new_ni.blk_addr, ni.blk_addr);
    } else {
      ASSERT_GT(new_ni.blk_addr, ni.blk_addr);
    }
  }
}

TEST_F(SegmentManagerTest, GetVictimSelPolicy) {
  VictimSelPolicy policy = fs_->GetSegmentManager().GetVictimSelPolicy(
      GcType::kFgGc, CursegType::kCursegHotNode, AllocMode::kSSR);
  ASSERT_EQ(policy.gc_mode, GcMode::kGcGreedy);
  ASSERT_EQ(policy.ofs_unit, 1U);

  policy = fs_->GetSegmentManager().GetVictimSelPolicy(GcType::kFgGc, CursegType::kNoCheckType,
                                                       AllocMode::kLFS);
  ASSERT_EQ(policy.gc_mode, GcMode::kGcGreedy);
  ASSERT_EQ(policy.ofs_unit, fs_->GetSuperblockInfo().GetSegsPerSec());
  ASSERT_EQ(policy.offset,
            fs_->GetSuperblockInfo().GetLastVictim(static_cast<int>(GcMode::kGcGreedy)));

  policy = fs_->GetSegmentManager().GetVictimSelPolicy(GcType::kBgGc, CursegType::kNoCheckType,
                                                       AllocMode::kLFS);
  ASSERT_EQ(policy.gc_mode, GcMode::kGcCb);
  ASSERT_EQ(policy.ofs_unit, fs_->GetSuperblockInfo().GetSegsPerSec());
  ASSERT_EQ(policy.offset, fs_->GetSuperblockInfo().GetLastVictim(static_cast<int>(GcMode::kGcCb)));

  DirtySeglistInfo *dirty_info = &fs_->GetSegmentManager().GetDirtySegmentInfo();
  dirty_info->nr_dirty[static_cast<int>(DirtyType::kDirty)] = kMaxSearchLimit + 2;
  policy = fs_->GetSegmentManager().GetVictimSelPolicy(GcType::kBgGc, CursegType::kNoCheckType,
                                                       AllocMode::kLFS);
  ASSERT_EQ(policy.max_search, kMaxSearchLimit);
}

TEST_F(SegmentManagerTest, GetMaxCost) {
  VictimSelPolicy policy = fs_->GetSegmentManager().GetVictimSelPolicy(
      GcType::kFgGc, CursegType::kCursegHotNode, AllocMode::kSSR);
  policy.min_cost = fs_->GetSegmentManager().GetMaxCost(policy);
  ASSERT_EQ(policy.min_cost,
            static_cast<uint32_t>(1 << fs_->GetSuperblockInfo().GetLogBlocksPerSeg()));

  policy = fs_->GetSegmentManager().GetVictimSelPolicy(GcType::kFgGc, CursegType::kNoCheckType,
                                                       AllocMode::kLFS);
  policy.min_cost = fs_->GetSegmentManager().GetMaxCost(policy);
  ASSERT_EQ(policy.min_cost,
            static_cast<uint32_t>(2 * (1 << fs_->GetSuperblockInfo().GetLogBlocksPerSeg()) *
                                  policy.ofs_unit));

  policy = fs_->GetSegmentManager().GetVictimSelPolicy(GcType::kBgGc, CursegType::kNoCheckType,
                                                       AllocMode::kLFS);
  policy.min_cost = fs_->GetSegmentManager().GetMaxCost(policy);
  ASSERT_EQ(policy.min_cost, std::numeric_limits<uint32_t>::max());
}

TEST_F(SegmentManagerTest, GetVictimByDefault) {
  DirtySeglistInfo *dirty_info = &fs_->GetSegmentManager().GetDirtySegmentInfo();

  uint32_t target_segno;
  for (target_segno = 0; target_segno < fs_->GetSegmentManager().TotalSegs(); ++target_segno) {
    if (!fs_->GetSegmentManager().SecUsageCheck(fs_->GetSegmentManager().GetSecNo(target_segno)) &&
        fs_->GetSegmentManager().GetValidBlocks(target_segno, 0) == 0U) {
      break;
    }
  }
  ASSERT_NE(target_segno, fs_->GetSegmentManager().TotalSegs());
  fs_->GetSegmentManager().GetSegmentEntry(target_segno).type =
      static_cast<uint8_t>(CursegType::kCursegHotNode);

  // 1. Test SSR victim
  fs_->GetSuperblockInfo().SetLastVictim(static_cast<int>(GcType::kBgGc), target_segno);
  if (!TestAndSetBit(target_segno,
                     dirty_info->dirty_segmap[static_cast<int>(DirtyType::kDirtyHotNode)].get())) {
    ++dirty_info->nr_dirty[static_cast<int>(DirtyType::kDirtyHotNode)];
  }

  auto victim_or = fs_->GetSegmentManager().GetVictimByDefault(
      GcType::kBgGc, CursegType::kCursegHotNode, AllocMode::kSSR);
  ASSERT_FALSE(victim_or.is_error());
  uint32_t get_victim = victim_or.value();
  ASSERT_EQ(get_victim, target_segno);

  // 2. Test FgGc victim
  fs_->GetSuperblockInfo().SetLastVictim(static_cast<int>(GcType::kFgGc), target_segno);
  if (!TestAndSetBit(target_segno,
                     dirty_info->dirty_segmap[static_cast<int>(DirtyType::kDirty)].get())) {
    ++dirty_info->nr_dirty[static_cast<int>(DirtyType::kDirty)];
  }

  victim_or = fs_->GetSegmentManager().GetVictimByDefault(GcType::kFgGc, CursegType::kNoCheckType,
                                                          AllocMode::kLFS);
  ASSERT_FALSE(victim_or.is_error());
  get_victim = victim_or.value();
  ASSERT_EQ(get_victim, target_segno);

  // 3. Skip if cur_victim_sec is set (SSR)
  ASSERT_EQ(fs_->GetGcManager().GetCurVictimSec(), fs_->GetSegmentManager().GetSecNo(target_segno));
  ASSERT_TRUE(TestBit(target_segno,
                      dirty_info->dirty_segmap[static_cast<int>(DirtyType::kDirtyHotNode)].get()));
  ASSERT_EQ(dirty_info->nr_dirty[static_cast<int>(DirtyType::kDirtyHotNode)], 1);
  victim_or = fs_->GetSegmentManager().GetVictimByDefault(GcType::kBgGc, CursegType::kCursegHotNode,
                                                          AllocMode::kSSR);
  ASSERT_TRUE(victim_or.is_error());

  // 4. Skip if victim_secmap is set (kBgGc)
  fs_->GetGcManager().SetCurVictimSec(kNullSecNo);
  ASSERT_TRUE(TestBit(target_segno,
                      dirty_info->dirty_segmap[static_cast<int>(DirtyType::kDirtyHotNode)].get()));
  ASSERT_EQ(dirty_info->nr_dirty[static_cast<int>(DirtyType::kDirty)], 1);
  SetBit(fs_->GetSegmentManager().GetSecNo(target_segno), dirty_info->victim_secmap.get());
  victim_or = fs_->GetSegmentManager().GetVictimByDefault(GcType::kBgGc, CursegType::kCursegHotNode,
                                                          AllocMode::kLFS);
  ASSERT_TRUE(victim_or.is_error());
}

TEST_F(SegmentManagerTest, AllocateNewSegments) {
  SuperblockInfo &superblock_info = fs_->GetSuperblockInfo();

  uint32_t temp_free_segment = fs_->GetSegmentManager().FreeSegments();
  fs_->GetSegmentManager().AllocateNewSegments();
  ASSERT_EQ(temp_free_segment - 3, fs_->GetSegmentManager().FreeSegments());

  superblock_info.ClearOpt(kMountDisableRollForward);
  temp_free_segment = fs_->GetSegmentManager().FreeSegments();
  for (int i = static_cast<int>(CursegType::kCursegHotNode);
       i <= static_cast<int>(CursegType::kCursegColdNode); ++i) {
    fs_->GetSegmentManager().AllocateSegmentByDefault(static_cast<CursegType>(i), true);
  }
  uint8_t type =
      superblock_info.GetCheckpoint().alloc_type[static_cast<int>(CursegType::kCursegHotNode)];
  ASSERT_EQ(superblock_info.GetSegmentCount(type), 6UL);
  ASSERT_EQ(temp_free_segment - 3, fs_->GetSegmentManager().FreeSegments());
}

TEST_F(SegmentManagerTest, DirtySegments) {
  // read the root inode block
  LockedPage root_node_page;
  SuperblockInfo &superblock_info = fs_->GetSuperblockInfo();
  fs_->GetNodeManager().GetNodePage(superblock_info.GetRootIno(), &root_node_page);
  ASSERT_NE(root_node_page, nullptr);

  DirtySeglistInfo &dirty_info = fs_->GetSegmentManager().GetDirtySegmentInfo();
  uint32_t dirtyDataSegments = dirty_info.nr_dirty[static_cast<int>(DirtyType::kDirtyHotData)] +
                               dirty_info.nr_dirty[static_cast<int>(DirtyType::kDirtyWarmData)] +
                               dirty_info.nr_dirty[static_cast<int>(DirtyType::kDirtyColdData)];

  uint32_t dirtyNodeSegments = dirty_info.nr_dirty[static_cast<int>(DirtyType::kDirtyHotNode)] +
                               dirty_info.nr_dirty[static_cast<int>(DirtyType::kDirtyWarmNode)] +
                               dirty_info.nr_dirty[static_cast<int>(DirtyType::kDirtyColdNode)];

  ASSERT_EQ(fs_->GetSegmentManager().DirtySegments(), dirtyDataSegments + dirtyNodeSegments);
}

TEST(SegmentManagerOptionTest, Section) {
  std::unique_ptr<Bcache> bc;
  MkfsOptions mkfs_options{};
  mkfs_options.segs_per_sec = 4;
  FileTester::MkfsOnFakeDevWithOptions(&bc, mkfs_options);

  std::unique_ptr<F2fs> fs;
  MountOptions mount_options{};
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  FileTester::MountWithOptions(loop.dispatcher(), mount_options, &bc, &fs);

  uint32_t blocks_per_section = kDefaultBlocksPerSegment * mkfs_options.segs_per_sec;
  SuperblockInfo &superblock_info = fs->GetSuperblockInfo();

  for (uint32_t i = 0; i < blocks_per_section; ++i) {
    NodeInfo ni;
    CursegInfo *cur_segment = fs->GetSegmentManager().CURSEG_I(CursegType::kCursegHotNode);

    {
      LockedPage root_node_page;
      fs->GetNodeManager().GetNodePage(superblock_info.GetRootIno(), &root_node_page);
      ASSERT_NE(root_node_page, nullptr);

      // Consume a block in the current section
      root_node_page->SetDirty();
    }
    WritebackOperation op = {.bSync = true};
    fs->GetNodeVnode().Writeback(op);

    fs->GetNodeManager().GetNodeInfo(superblock_info.GetRootIno(), ni);
    ASSERT_NE(ni.blk_addr, kNullAddr);
    ASSERT_NE(ni.blk_addr, kNewAddr);

    unsigned int expected = 1;
    // When a new secton is allocated, the valid block count of the previous one should be zero
    if ((ni.blk_addr + 1) % blocks_per_section == 0) {
      expected = 0;
    }
    ASSERT_EQ(expected,
              fs->GetSegmentManager().GetValidBlocks(
                  cur_segment->segno, safemath::checked_cast<int>(mkfs_options.segs_per_sec)));
    ASSERT_FALSE(fs->GetSegmentManager().HasNotEnoughFreeSecs());
  }

  FileTester::Unmount(std::move(fs), &bc);
}

TEST(SegmentManagerOptionTest, GetNewSegmentHeap) {
  std::unique_ptr<Bcache> bc;
  MkfsOptions mkfs_options{};
  mkfs_options.heap_based_allocation = true;
  mkfs_options.segs_per_sec = 4;
  mkfs_options.secs_per_zone = 4;
  FileTester::MkfsOnFakeDevWithOptions(&bc, mkfs_options);

  std::unique_ptr<F2fs> fs;
  MountOptions mount_options{};
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  FileTester::MountWithOptions(loop.dispatcher(), mount_options, &bc, &fs);

  // Clear kMountNoheap opt, Allocate a new segment for hot nodes
  SuperblockInfo &superblock_info = fs->GetSuperblockInfo();
  superblock_info.ClearOpt(kMountNoheap);
  fs->GetSegmentManager().NewCurseg(CursegType::kCursegHotNode, false);

  const uint32_t alloc_size = kDefaultBlocksPerSegment * mkfs_options.segs_per_sec;
  uint32_t nwritten = alloc_size * mkfs_options.secs_per_zone * 3;

  for (uint32_t i = 0; i < nwritten; ++i) {
    NodeInfo ni, new_ni;
    fs->GetNodeManager().GetNodeInfo(superblock_info.GetRootIno(), ni);
    ASSERT_NE(ni.blk_addr, kNullAddr);
    ASSERT_NE(ni.blk_addr, kNewAddr);

    {
      LockedPage root_node_page;
      fs->GetNodeManager().GetNodePage(superblock_info.GetRootIno(), &root_node_page);
      ASSERT_NE(root_node_page, nullptr);
      root_node_page->SetDirty();
    }

    WritebackOperation op = {.bSync = true};
    fs->GetNodeVnode().Writeback(op);

    fs->GetNodeManager().GetNodeInfo(superblock_info.GetRootIno(), new_ni);
    ASSERT_NE(new_ni.blk_addr, kNullAddr);
    ASSERT_NE(new_ni.blk_addr, kNewAddr);

    // The heap style allocation tries to find a free node section from the end of main area
    if ((i > alloc_size * 2 - 1) && (new_ni.blk_addr % alloc_size == 0)) {
      ASSERT_LT(new_ni.blk_addr, ni.blk_addr);
    } else {
      ASSERT_GT(new_ni.blk_addr, ni.blk_addr);
    }
  }

  FileTester::Unmount(std::move(fs), &bc);
}

TEST(SegmentManagerOptionTest, GetNewSegmentNoHeap) {
  std::unique_ptr<Bcache> bc;
  MkfsOptions mkfs_options{};
  mkfs_options.heap_based_allocation = false;
  mkfs_options.segs_per_sec = 4;
  mkfs_options.secs_per_zone = 4;
  FileTester::MkfsOnFakeDevWithOptions(&bc, mkfs_options);

  std::unique_ptr<F2fs> fs;
  MountOptions mount_options;
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  FileTester::MountWithOptions(loop.dispatcher(), mount_options, &bc, &fs);

  // Set kMountNoheap opt, Allocate a new segment for hot nodes
  SuperblockInfo &superblock_info = fs->GetSuperblockInfo();
  superblock_info.SetOpt(kMountNoheap);
  fs->GetSegmentManager().NewCurseg(CursegType::kCursegHotNode, false);

  uint32_t nwritten =
      kDefaultBlocksPerSegment * mkfs_options.segs_per_sec * mkfs_options.secs_per_zone * 3;

  for (uint32_t i = 0; i < nwritten; ++i) {
    NodeInfo ni, new_ni;
    fs->GetNodeManager().GetNodeInfo(superblock_info.GetRootIno(), ni);
    ASSERT_NE(ni.blk_addr, kNullAddr);
    ASSERT_NE(ni.blk_addr, kNewAddr);

    {
      LockedPage root_node_page;
      fs->GetNodeManager().GetNodePage(superblock_info.GetRootIno(), &root_node_page);
      ASSERT_NE(root_node_page, nullptr);
      root_node_page->SetDirty();
    }
    WritebackOperation op = {.bSync = true};
    fs->GetNodeVnode().Writeback(op);

    fs->GetNodeManager().GetNodeInfo(superblock_info.GetRootIno(), new_ni);
    ASSERT_NE(new_ni.blk_addr, kNullAddr);
    ASSERT_NE(new_ni.blk_addr, kNewAddr);
    // It tries to find a free nodesction from the start of main area
    ASSERT_GT(new_ni.blk_addr, ni.blk_addr);
  }

  FileTester::Unmount(std::move(fs), &bc);
}

TEST(SegmentManagerOptionTest, DestroySegmentManagerExceptionCase) {
  std::unique_ptr<Bcache> bc;
  MkfsOptions mkfs_options{};
  FileTester::MkfsOnFakeDevWithOptions(&bc, mkfs_options);

  MountOptions mount_options;
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);

  auto superblock = F2fs::LoadSuperblock(*bc);
  ASSERT_TRUE(superblock.is_ok());
  // Create a vfs object for unit tests.
  auto vfs_or = Runner::CreateRunner(loop.dispatcher());
  ZX_ASSERT(vfs_or.is_ok());
  std::unique_ptr<F2fs> fs = std::make_unique<F2fs>(
      loop.dispatcher(), std::move(bc), std::move(*superblock), mount_options, (*vfs_or).get());

  ASSERT_EQ(fs->FillSuper(), ZX_OK);

  fs->WriteCheckpoint(false, true);

  // fault injection
  fs->GetSegmentManager().SetDirtySegmentInfo(nullptr);
  fs->GetSegmentManager().SetFreeSegmentInfo(nullptr);
  fs->GetSegmentManager().DestroySitInfo();
  fs->GetSegmentManager().SetSitInfo(nullptr);
  fs->GetSegmentManager().DestroySitInfo();

  fs->ResetPsuedoVnodes();
  fs->GetVCache().Reset();
  fs->GetNodeManager().DestroyNodeManager();

  // test exception case
  fs->GetSegmentManager().DestroySegmentManager();
}

TEST(SegmentManagerOptionTest, ModeLfs) {
  std::unique_ptr<Bcache> bc;
  MkfsOptions mkfs_options{};
  mkfs_options.segs_per_sec = 4;
  FileTester::MkfsOnFakeDevWithOptions(&bc, mkfs_options);

  std::unique_ptr<F2fs> fs;
  MountOptions mount_options;
  mount_options.SetValue("mode", static_cast<uint32_t>(ModeType::kModeLfs));
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  FileTester::MountWithOptions(loop.dispatcher(), mount_options, &bc, &fs);
  fbl::RefPtr<VnodeF2fs> root;
  FileTester::CreateRoot(fs.get(), &root);
  auto root_dir = fbl::RefPtr<Dir>::Downcast(std::move(root));

  ASSERT_EQ(fs->GetSuperblockInfo().TestOpt(kMountForceLfs), true);
  ASSERT_EQ(fs->GetSegmentManager().NeedSSR(), false);

  // Make SSR, IPU condition
  FileTester::CreateChild(root_dir.get(), S_IFREG, "alpha");
  fbl::RefPtr<fs::Vnode> vn;
  FileTester::Lookup(root_dir.get(), "alpha", &vn);
  auto file = fbl::RefPtr<File>::Downcast(std::move(vn));
  char buf[4 * kPageSize] = {
      1,
  };
  while (!fs->GetSegmentManager().NeedInplaceUpdate(file.get())) {
    size_t out_end, out_actual;
    if (auto ret = file->Append(buf, sizeof(buf), &out_end, &out_actual); ret == ZX_ERR_NO_SPACE) {
      break;
    } else {
      ASSERT_EQ(ret, ZX_OK);
    }
    WritebackOperation op = {.bSync = true};
    fs->SyncDirtyDataPages(op);
  }

  // Since kMountForceLfs is on, f2fs doesn't allocate segments in ssr manner.
  ASSERT_EQ(fs->GetSegmentManager().NeedSSR(), false);
  ASSERT_EQ(fs->GetSegmentManager().NeedInplaceUpdate(file.get()), false);

  // Make SSR, IPU enable
  fs->GetSuperblockInfo().ClearOpt(kMountForceLfs);
  ASSERT_EQ(fs->GetSegmentManager().NeedSSR(), true);

  EXPECT_EQ(file->Close(), ZX_OK);
  file = nullptr;

  // Test ClearPrefreeSegments()
  fs->GetSuperblockInfo().SetOpt(kMountForceLfs);
  FileTester::DeleteChild(root_dir.get(), "alpha", false);
  fs->WriteCheckpoint(false, false);

  EXPECT_EQ(root_dir->Close(), ZX_OK);
  root_dir = nullptr;
  FileTester::Unmount(std::move(fs), &bc);
  EXPECT_EQ(Fsck(std::move(bc), FsckOptions{.repair = false}, &bc), ZX_OK);
}

}  // namespace
}  // namespace f2fs
