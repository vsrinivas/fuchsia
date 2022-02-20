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
  fbl::RefPtr<Page> root_node_page = nullptr;
  SuperblockInfo &superblock_info = fs_->GetSuperblockInfo();
  std::vector<block_t> blk_chain(0);
  int nwritten = kDefaultBlocksPerSegment * 2;
  // write the root inode, and read the block where the previous version of the root inode is stored
  // to check if the block has a proper lba address to the next node block
  for (int i = 0; i < nwritten; ++i) {
    fbl::RefPtr<Page> read_page = nullptr;
    NodeInfo ni;

    fs_->GetNodeManager().GetNodePage(superblock_info.GetRootIno(), &read_page);
    blk_chain.push_back(NodeManager::NextBlkaddrOfNode(*read_page));
    read_page->SetDirty();
    Page::PutPage(std::move(read_page), true);
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
    fbl::RefPtr<Page> read_page = nullptr;
    NodeInfo ni;

    fs_->GetNodeManager().GetNodePage(superblock_info.GetRootIno(), &read_page);
    fs_->GetNodeManager().GetNodeInfo(superblock_info.GetRootIno(), ni);
    ASSERT_NE(ni.blk_addr, kNullAddr);
    ASSERT_NE(ni.blk_addr, kNewAddr);
    block_t old_addr = ni.blk_addr;

    read_page->SetDirty();
    Page::PutPage(std::move(read_page), true);
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
  fs_->GetSegmentManager().NeedToCheckpoint();

  ASSERT_EQ(fs_->GetSegmentManager().FreeSegments(), nfree_segs);
  ASSERT_FALSE(fs_->GetSegmentManager().PrefreeSegments());

  superblock_info.SetOnRecovery();
  fs_->GetSegmentManager().BalanceFs();
  fs_->GetSegmentManager().NeedToCheckpoint();

  ASSERT_EQ(fs_->GetSegmentManager().FreeSegments(), nfree_segs);
  ASSERT_FALSE(fs_->GetSegmentManager().PrefreeSegments());
}

TEST_F(SegmentManagerTest, InvalidateBlocksExceptionCase) {
  // read the root inode block
  fbl::RefPtr<Page> root_node_page = nullptr;
  SuperblockInfo &superblock_info = fs_->GetSuperblockInfo();
  fs_->GetNodeManager().GetNodePage(superblock_info.GetRootIno(), &root_node_page);
  ASSERT_NE(root_node_page, nullptr);

  // Check InvalidateBlocks() exception case
  block_t temp_written_valid_blocks = fs_->GetSegmentManager().GetSitInfo().written_valid_blocks;
  fs_->GetSegmentManager().InvalidateBlocks(kNewAddr);
  ASSERT_EQ(temp_written_valid_blocks, fs_->GetSegmentManager().GetSitInfo().written_valid_blocks);

  Page::PutPage(std::move(root_node_page), true);
}

TEST_F(SegmentManagerTest, GetNewSegmentHeap) {
  // read the root inode block
  fbl::RefPtr<Page> root_node_page = nullptr;
  SuperblockInfo &superblock_info = fs_->GetSuperblockInfo();

  // Check GetNewSegment() on AllocDirection::kAllocLeft
  superblock_info.ClearOpt(kMountNoheap);
  uint32_t nwritten = kDefaultBlocksPerSegment * 3;

  for (uint32_t i = 0; i < nwritten; ++i) {
    fbl::RefPtr<Page> read_page = nullptr;
    NodeInfo ni, new_ni;

    fs_->GetNodeManager().GetNodePage(superblock_info.GetRootIno(), &read_page);
    fs_->GetNodeManager().GetNodeInfo(superblock_info.GetRootIno(), ni);
    ASSERT_NE(ni.blk_addr, kNullAddr);
    ASSERT_NE(ni.blk_addr, kNewAddr);

    read_page->SetDirty();
    Page::PutPage(std::move(read_page), true);
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

TEST_F(SegmentManagerTest, GetMaxCost) {
  // Get the maximum cost on cost-benefit GC mode
  VictimSelPolicy p;
  p.alloc_mode = AllocMode::kSSR;
  p.min_segno = kNullSegNo;
  fs_->GetSegmentManager().SelectPolicy(GcType::kBgGc, CursegType::kCursegHotNode, &p);

  p.gc_mode = GcMode::kGcGreedy;
  p.min_cost = fs_->GetSegmentManager().GetMaxCost(&p);
  ASSERT_EQ(p.min_cost,
            static_cast<uint32_t>(1 << fs_->GetSuperblockInfo().GetLogBlocksPerSeg() * p.ofs_unit));

  p.gc_mode = GcMode::kGcCb;
  p.min_cost = fs_->GetSegmentManager().GetMaxCost(&p);
  ASSERT_EQ(p.min_cost, std::numeric_limits<uint32_t>::max());
}

TEST_F(SegmentManagerTest, GetVictimByDefault) {
  int nwritten = kMaxSearchLimit + 2;
  DirtySeglistInfo *dirty_info = &fs_->GetSegmentManager().GetDirtySegmentInfo();

  // 1. Dirty current segment
  CursegInfo *curseg = fs_->GetSegmentManager().CURSEG_I(CursegType::kCursegHotNode);
  FileTester::CreateChild(root_dir_.get(), S_IFDIR, "GetVictimByDefault_current_segment");
  ASSERT_NE(fs_->GetSegmentManager().GetValidBlocks(curseg->segno, 0), 0U);
  TestAndSetBit(curseg->segno,
                dirty_info->dirty_segmap[static_cast<int>(DirtyType::kDirtyHotNode)].get());
  ++dirty_info->nr_dirty[static_cast<int>(DirtyType::kDirtyHotNode)];

  curseg = fs_->GetSegmentManager().CURSEG_I(CursegType::kCursegHotNode);
  fs_->GetSegmentManager().GetVictimByDefault(GcType::kBgGc, CursegType::kCursegHotNode,
                                              AllocMode::kSSR, &(curseg->next_segno));

  // 2. Skip if victim_segmap is set (kFgGc)
  curseg = fs_->GetSegmentManager().CURSEG_I(CursegType::kCursegHotNode);
  FileTester::CreateChild(root_dir_.get(), S_IFDIR, "GetVictimByDefault_kFgGc");
  ASSERT_NE(fs_->GetSegmentManager().GetValidBlocks(curseg->segno, 0), 0U);
  fs_->GetSegmentManager().AllocateSegmentByDefault(CursegType::kCursegHotNode, true);
  fs_->GetSegmentManager().LocateDirtySegment(curseg->segno);
  SetBit(curseg->segno, dirty_info->victim_segmap[static_cast<int>(GcType::kFgGc)].get());

  // 3. Skip if victim_segmap is set (kBgGc)
  curseg = fs_->GetSegmentManager().CURSEG_I(CursegType::kCursegHotNode);
  FileTester::CreateChild(root_dir_.get(), S_IFDIR, "GetVictimByDefault_kBgGc");
  fs_->WriteCheckpoint(false, false);
  ASSERT_NE(fs_->GetSegmentManager().GetValidBlocks(curseg->segno, 0), 0U);
  fs_->GetSegmentManager().AllocateSegmentByDefault(CursegType::kCursegHotNode, true);
  fs_->GetSegmentManager().LocateDirtySegment(curseg->segno);
  SetBit(curseg->segno, dirty_info->victim_segmap[static_cast<int>(GcType::kBgGc)].get());

  curseg = fs_->GetSegmentManager().CURSEG_I(CursegType::kCursegHotNode);
  fs_->GetSegmentManager().GetVictimByDefault(GcType::kBgGc, CursegType::kCursegHotNode,
                                              AllocMode::kSSR, &(curseg->next_segno));

  // 4. Search dirty_map to kMaxSearchLimit
  for (int i = 0; i < nwritten; ++i) {
    CursegInfo *curseg = fs_->GetSegmentManager().CURSEG_I(CursegType::kCursegHotNode);
    FileTester::CreateChild(root_dir_.get(), S_IFDIR,
                            "GetVictimByDefault_kMaxSearchLimit_" + std::to_string(i));
    fs_->WriteCheckpoint(false, false);
    ASSERT_NE(fs_->GetSegmentManager().GetValidBlocks(curseg->segno, 0), 0U);
    fs_->GetSegmentManager().AllocateSegmentByDefault(CursegType::kCursegHotNode, true);
    fs_->GetSegmentManager().LocateDirtySegment(curseg->segno);
  }

  curseg = fs_->GetSegmentManager().CURSEG_I(CursegType::kCursegHotNode);
  fs_->GetSegmentManager().GetVictimByDefault(GcType::kBgGc, CursegType::kCursegHotNode,
                                              AllocMode::kSSR, &(curseg->next_segno));

  // 4. Search dirty_map from last_victim
  for (int i = 0; i < nwritten; ++i) {
    CursegInfo *curseg = fs_->GetSegmentManager().CURSEG_I(CursegType::kCursegHotNode);
    FileTester::CreateChild(root_dir_.get(), S_IFDIR,
                            "GetVictimByDefault_last_victim_" + std::to_string(i));
    fs_->WriteCheckpoint(false, false);
    ASSERT_NE(fs_->GetSegmentManager().GetValidBlocks(curseg->segno, 0), 0U);
    fs_->GetSegmentManager().AllocateSegmentByDefault(CursegType::kCursegHotNode, true);
    fs_->GetSegmentManager().LocateDirtySegment(curseg->segno);
  }

  curseg = fs_->GetSegmentManager().CURSEG_I(CursegType::kCursegHotNode);
  fs_->GetSegmentManager().GetVictimByDefault(GcType::kBgGc, CursegType::kCursegHotNode,
                                              AllocMode::kSSR, &(curseg->next_segno));
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
  fbl::RefPtr<Page> root_node_page = nullptr;
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

  Page::PutPage(std::move(root_node_page), true);
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
    fbl::RefPtr<Page> root_node_page = nullptr;
    CursegInfo *cur_segment = fs->GetSegmentManager().CURSEG_I(CursegType::kCursegHotNode);
    fs->GetNodeManager().GetNodePage(superblock_info.GetRootIno(), &root_node_page);
    ASSERT_NE(root_node_page, nullptr);

    // Consume a block in the current section
    root_node_page->SetDirty();
    Page::PutPage(std::move(root_node_page), true);
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
    fbl::RefPtr<Page> root_node_page = nullptr;
    fs->GetNodeManager().GetNodePage(superblock_info.GetRootIno(), &root_node_page);
    ASSERT_NE(root_node_page, nullptr);

    fs->GetNodeManager().GetNodeInfo(superblock_info.GetRootIno(), ni);
    ASSERT_NE(ni.blk_addr, kNullAddr);
    ASSERT_NE(ni.blk_addr, kNewAddr);

    root_node_page->SetDirty();
    Page::PutPage(std::move(root_node_page), true);
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
    fbl::RefPtr<Page> root_node_page = nullptr;
    fs->GetNodeManager().GetNodePage(superblock_info.GetRootIno(), &root_node_page);
    ASSERT_NE(root_node_page, nullptr);

    fs->GetNodeManager().GetNodeInfo(superblock_info.GetRootIno(), ni);
    ASSERT_NE(ni.blk_addr, kNullAddr);
    ASSERT_NE(ni.blk_addr, kNewAddr);

    root_node_page->SetDirty();
    Page::PutPage(std::move(root_node_page), true);
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

  auto superblock = std::make_unique<Superblock>();
  ASSERT_EQ(LoadSuperblock(bc.get(), superblock.get()), ZX_OK);
  std::unique_ptr<F2fs> fs = std::make_unique<F2fs>(loop.dispatcher(), std::move(bc),
                                                    std::move(superblock), mount_options);

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

}  // namespace
}  // namespace f2fs
