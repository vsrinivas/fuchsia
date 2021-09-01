// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <unordered_set>

#include <block-client/cpp/fake-device.h>
#include <gtest/gtest.h>

#include "src/storage/f2fs/f2fs.h"
#include "unit_lib.h"

namespace f2fs {
namespace {

TEST(SegmentMgr, BlkChaining) {
  std::unique_ptr<Bcache> bc;
  unittest_lib::MkfsOnFakeDev(&bc);

  // create f2fs and root dir
  std::unique_ptr<F2fs> fs;
  MountOptions options{};
  ASSERT_EQ(options.SetValue(options.GetNameView(kOptInlineDentry), 0), ZX_OK);
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  unittest_lib::MountWithOptions(loop.dispatcher(), options, &bc, &fs);
  fbl::RefPtr<VnodeF2fs> root;
  unittest_lib::CreateRoot(fs.get(), &root);
  Page *root_node_page = nullptr;
  SbInfo &sbi = fs->GetSbInfo();

  // read the node block where the root inode is stored
  fs->Nodemgr().GetNodePage(RootIno(&sbi), &root_node_page);
  ASSERT_TRUE(root_node_page);

  // retrieve the lba for root inode
  std::vector<block_t> blk_chain(0);
  int nwritten = kDefaultBlocksPerSegment * 2;
  NodeInfo ni;
  fs->Nodemgr().GetNodeInfo(RootIno(&sbi), &ni);
  ASSERT_NE(ni.blk_addr, kNullAddr);
  ASSERT_NE(ni.blk_addr, kNewAddr);
  block_t alloc_addr = ni.blk_addr;
  blk_chain.push_back(alloc_addr);

  // write the root inode, and read the block where the previous version of the root inode is stored
  // to check if the block has a proper lba address to the next node block
  for (int i = 0; i < nwritten; i++) {
    block_t old_addr = alloc_addr;
    Page *read_page = GrabCachePage(nullptr, 0, 0);
    ASSERT_TRUE(read_page);

    fs->Segmgr().WriteNodePage(root_node_page, RootIno(&sbi), old_addr, &alloc_addr);
    blk_chain.push_back(alloc_addr);
    ASSERT_NE(alloc_addr, kNullAddr);
    ASSERT_EQ(fs->GetBc().Readblk(blk_chain[i], read_page->data), ZX_OK);
    ASSERT_EQ(alloc_addr, fs->Nodemgr().NextBlkaddrOfNode(read_page));
    F2fsPutPage(read_page, 0);
  }

  F2fsPutPage(root_node_page, 0);
  ASSERT_EQ(root->Close(), ZX_OK);
  root = nullptr;

  unittest_lib::Unmount(std::move(fs), &bc);
}

TEST(SegmentMgr, DirtyToFree) {
  std::unique_ptr<Bcache> bc;
  unittest_lib::MkfsOnFakeDev(&bc);

  // create f2fs and root dir
  std::unique_ptr<F2fs> fs;
  MountOptions options{};
  ASSERT_EQ(options.SetValue(options.GetNameView(kOptInlineDentry), 0), ZX_OK);
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  unittest_lib::MountWithOptions(loop.dispatcher(), options, &bc, &fs);
  fbl::RefPtr<VnodeF2fs> root;
  unittest_lib::CreateRoot(fs.get(), &root);

  // read the root inode block
  Page *root_node_page = nullptr;
  SbInfo &sbi = fs->GetSbInfo();
  fs->Nodemgr().GetNodePage(RootIno(&sbi), &root_node_page);
  ASSERT_TRUE(root_node_page);

  // check the precond. before making dirty segments
  std::vector<uint32_t> prefree_array(0);
  int nwritten = kDefaultBlocksPerSegment * 2;
  uint32_t nprefree = 0;
  NodeInfo ni;
  fs->Nodemgr().GetNodeInfo(RootIno(&sbi), &ni);
  ASSERT_NE(ni.blk_addr, kNullAddr);
  ASSERT_NE(ni.blk_addr, kNewAddr);
  block_t alloc_addr = ni.blk_addr;
  ASSERT_FALSE(fs->Segmgr().PrefreeSegments());
  uint32_t nfree_segs = fs->Segmgr().FreeSegments();
  FreeSegmapInfo *free_i = GetFreeInfo(&sbi);
  DirtySeglistInfo *dirty_i = GetDirtyInfo(&sbi);

  // write the root inode repeatedly as much as 2 segments
  for (int i = 0; i < nwritten; i++) {
    block_t old_addr = alloc_addr;
    fs->Segmgr().WriteNodePage(root_node_page, RootIno(&sbi), old_addr, &alloc_addr);
    if (fs->Segmgr().GetValidBlocks(GetSegNo(&sbi, old_addr), 0) == 0) {
      prefree_array.push_back(GetSegNo(&sbi, old_addr));
      ASSERT_EQ(fs->Segmgr().PrefreeSegments(), ++nprefree);
    }
  }

  // check the bitmaps and the number of free/prefree segments
  ASSERT_EQ(fs->Segmgr().FreeSegments(), nfree_segs - nprefree);
  for (auto &pre : prefree_array) {
    ASSERT_TRUE(TestBit(pre, dirty_i->dirty_segmap[static_cast<int>(DirtyType::kPre)]));
    ASSERT_TRUE(TestBit(pre, free_i->free_segmap));
  }
  // triggers checkpoint to make prefree segments transit to free ones
  fs->Segmgr().BalanceFs();

  // check the bitmaps and the number of free/prefree segments
  for (auto &pre : prefree_array) {
    ASSERT_FALSE(TestBit(pre, dirty_i->dirty_segmap[static_cast<int>(DirtyType::kPre)]));
    ASSERT_FALSE(TestBit(pre, free_i->free_segmap));
  }
  ASSERT_EQ(fs->Segmgr().FreeSegments(), nfree_segs);
  ASSERT_FALSE(fs->Segmgr().PrefreeSegments());

  F2fsPutPage(root_node_page, 0);
  ASSERT_EQ(root->Close(), ZX_OK);
  root = nullptr;

  unittest_lib::Unmount(std::move(fs), &bc);
}

}  // namespace
}  // namespace f2fs
