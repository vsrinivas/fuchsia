// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/syslog/cpp/macros.h>

#include <numeric>
#include <vector>

#include <block-client/cpp/fake-device.h>
#include <gtest/gtest.h>

#include "src/storage/f2fs/f2fs.h"
#include "unit_lib.h"

namespace f2fs {
namespace {

constexpr uint32_t kMaxNodeCnt = 10;

TEST(NodeManagerTest, NatCache) {
  std::unique_ptr<Bcache> bc;
  FileTester::MkfsOnFakeDev(&bc);

  std::unique_ptr<F2fs> fs;
  MountOptions options{};
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  FileTester::MountWithOptions(loop.dispatcher(), options, &bc, &fs);

  NodeManager &node_manager = fs->GetNodeManager();

  fbl::RefPtr<VnodeF2fs> root;
  FileTester::CreateRoot(fs.get(), &root);
  fbl::RefPtr<Dir> root_dir = fbl::RefPtr<Dir>::Downcast(std::move(root));
  size_t num_tree = 0, num_clean = 0, num_dirty = 0;

  // 1. Check NAT cache is empty
  MapTester::GetNatCacheEntryCount(node_manager, num_tree, num_clean, num_dirty);
  ASSERT_EQ(num_tree, 1UL);
  ASSERT_EQ(num_clean, 1UL);  // root inode
  ASSERT_EQ(num_dirty, 0UL);
  // NatEntry *entry = nullptr;
  // entry = &node_manager->clean_nat_list_.front();
  // ASSERT_EQ(entry->GetNid(), static_cast<nid_t>(RootIno(&sbi)));

  // 2. Check NAT entry is cached in dirty NAT entries list
  std::vector<fbl::RefPtr<VnodeF2fs>> vnodes;
  std::vector<uint32_t> inos;

  // Fill NAT cache
  FileTester::CreateChildren(fs.get(), vnodes, inos, root_dir, "NATCache_", kMaxNodeCnt);
  ASSERT_EQ(vnodes.size(), kMaxNodeCnt);
  ASSERT_EQ(inos.size(), kMaxNodeCnt);

  MapTester::GetNatCacheEntryCount(node_manager, num_tree, num_clean, num_dirty);
  ASSERT_EQ(num_tree, static_cast<size_t>(kMaxNodeCnt + 1));
  ASSERT_EQ(num_clean, static_cast<size_t>(0));
  ASSERT_EQ(num_dirty, static_cast<size_t>(kMaxNodeCnt + 1));
  ASSERT_EQ(node_manager.GetNatCount(), static_cast<nid_t>(kMaxNodeCnt + 1));

  // Lookup NAT cache
  for (auto ino : inos) {
    NodeInfo ni;
    ASSERT_TRUE(MapTester::IsCachedNat(node_manager, ino));
    fs->GetNodeManager().GetNodeInfo(ino, ni);
    ASSERT_EQ(ni.nid, ino);
  }

  // Move dirty entries to clean entries
  fs->GetNodeManager().FlushNatEntries();

  // 3. Check NAT entry is cached in clean NAT entries list
  MapTester::GetNatCacheEntryCount(node_manager, num_tree, num_clean, num_dirty);
  ASSERT_EQ(num_tree, static_cast<size_t>(kMaxNodeCnt + 1));
  ASSERT_EQ(num_clean, static_cast<size_t>(kMaxNodeCnt + 1));
  ASSERT_EQ(num_dirty, static_cast<size_t>(0));
  ASSERT_EQ(node_manager.GetNatCount(), static_cast<nid_t>(kMaxNodeCnt + 1));

  // Lookup NAT cache
  for (auto ino : inos) {
    NodeInfo ni;
    ASSERT_TRUE(MapTester::IsCachedNat(node_manager, ino));
    fs->GetNodeManager().GetNodeInfo(ino, ni);
    ASSERT_EQ(ni.nid, ino);
  }

  // 4. Flush all NAT cache entries
  MapTester::RemoveAllNatEntries(node_manager);
  ASSERT_EQ(node_manager.GetNatCount(), static_cast<uint32_t>(0));

  CursegInfo *curseg = fs->GetSegmentManager().CURSEG_I(CursegType::kCursegHotData);  // NAT Journal
  SummaryBlock *sum = curseg->sum_blk;

  MapTester::GetNatCacheEntryCount(node_manager, num_tree, num_clean, num_dirty);
  ASSERT_EQ(num_tree, static_cast<size_t>(0));
  ASSERT_EQ(num_clean, static_cast<size_t>(0));
  ASSERT_EQ(num_dirty, static_cast<size_t>(0));
  ASSERT_EQ(NatsInCursum(sum), static_cast<int>(kMaxNodeCnt + 1));

  // Lookup NAT journal
  for (auto ino : inos) {
    NodeInfo ni;
    ASSERT_FALSE(MapTester::IsCachedNat(node_manager, ino));
    fs->GetNodeManager().GetNodeInfo(ino, ni);
    ASSERT_EQ(ni.nid, ino);
  }

  // 5. Check NAT cache miss and journal miss
  std::vector<uint32_t> journal_inos;

  // Fill NAT cache with journal size -2
  // Root inode NAT(nid=4) is duplicated in cache and journal, so we need to keep two empty NAT
  // entries
  FileTester::CreateChildren(fs.get(), vnodes, journal_inos, root_dir, "NATJournal_",
                             kNatJournalEntries - kMaxNodeCnt - 2);
  ASSERT_EQ(vnodes.size(), kNatJournalEntries - 2);
  ASSERT_EQ(inos.size() + journal_inos.size(), kNatJournalEntries - 2);

  // Fill NAT journal
  fs->GetNodeManager().FlushNatEntries();
  ASSERT_EQ(NatsInCursum(sum), static_cast<int>(kNatJournalEntries - 1));

  // Fill NAT cache over journal size
  FileTester::CreateChildren(fs.get(), vnodes, journal_inos, root_dir, "NATJournalFlush_", 2);
  ASSERT_EQ(vnodes.size(), kNatJournalEntries);
  ASSERT_EQ(inos.size() + journal_inos.size(), kNatJournalEntries);

  // Flush NAT journal
  fs->GetNodeManager().FlushNatEntries();
  ASSERT_EQ(NatsInCursum(sum), static_cast<int>(0));

  // Flush NAT cache
  MapTester::RemoveAllNatEntries(node_manager);
  ASSERT_EQ(node_manager.GetNatCount(), static_cast<uint32_t>(0));

  // Check NAT cache empty
  MapTester::GetNatCacheEntryCount(node_manager, num_tree, num_clean, num_dirty);
  ASSERT_EQ(num_tree, static_cast<size_t>(0));
  ASSERT_EQ(num_clean, static_cast<size_t>(0));
  ASSERT_EQ(num_dirty, static_cast<size_t>(0));
  ASSERT_EQ(node_manager.GetNatCount(), static_cast<nid_t>(0));

  // Read NAT block
  for (auto ino : inos) {
    NodeInfo ni;
    ASSERT_FALSE(MapTester::IsCachedNat(node_manager, ino));
    fs->GetNodeManager().GetNodeInfo(ino, ni);
    ASSERT_EQ(ni.nid, ino);
  }

  MapTester::GetNatCacheEntryCount(node_manager, num_tree, num_clean, num_dirty);
  ASSERT_EQ(num_tree, static_cast<size_t>(10));
  ASSERT_EQ(num_clean, static_cast<size_t>(10));
  ASSERT_EQ(num_dirty, static_cast<size_t>(0));
  ASSERT_EQ(node_manager.GetNatCount(), static_cast<nid_t>(10));

  // Shrink nat cache to reduce memory usage (test TryToFreeNats())
  MapTester::SetNatCount(node_manager, node_manager.GetNatCount() + kNmWoutThreshold * 3);
  fs->WriteCheckpoint(false, false);

  MapTester::GetNatCacheEntryCount(node_manager, num_tree, num_clean, num_dirty);
  ASSERT_EQ(num_tree, static_cast<size_t>(0));
  ASSERT_EQ(num_clean, static_cast<size_t>(0));
  ASSERT_EQ(node_manager.GetNatCount(), static_cast<uint32_t>(kNmWoutThreshold * 3));
  MapTester::SetNatCount(node_manager, 0);

  for (auto &vnode_refptr : vnodes) {
    ASSERT_EQ(vnode_refptr->Close(), ZX_OK);
    vnode_refptr.reset();
  }

  ASSERT_EQ(root_dir->Close(), ZX_OK);
  root_dir.reset();

  FileTester::Unmount(std::move(fs), &bc);
}

TEST(NodeManagerTest, FreeNid) {
  std::unique_ptr<Bcache> bc;
  FileTester::MkfsOnFakeDev(&bc);

  std::unique_ptr<F2fs> fs;
  MountOptions options{};
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  FileTester::MountWithOptions(loop.dispatcher(), options, &bc, &fs);

  NodeManager &node_manager = fs->GetNodeManager();

  ASSERT_EQ(node_manager.GetFirstScanNid(), static_cast<nid_t>(4));

  nid_t nid = node_manager.GetFirstScanNid();
  nid_t init_fcnt = node_manager.GetFreeNidCount();

  nid = MapTester::ScanFreeNidList(node_manager, nid);
  ASSERT_EQ(nid, node_manager.GetNextScanNid());

  // Alloc Done
  fs->GetNodeManager().AllocNid(&nid);
  ASSERT_EQ(nid, static_cast<nid_t>(4));
  ASSERT_EQ(node_manager.GetFreeNidCount(), init_fcnt - 1);

  FreeNid *fi = MapTester::GetNextFreeNidInList(node_manager);
  ASSERT_EQ(fi->nid, static_cast<nid_t>(4));
  ASSERT_EQ(fi->state, static_cast<int>(NidState::kNidAlloc));

  fs->GetNodeManager().AllocNidDone(nid);
  fi = MapTester::GetNextFreeNidInList(node_manager);
  ASSERT_EQ(fi->nid, static_cast<nid_t>(5));
  ASSERT_EQ(fi->state, static_cast<int>(NidState::kNidNew));

  // Alloc Failed
  fs->GetNodeManager().AllocNid(&nid);
  ASSERT_EQ(nid, static_cast<nid_t>(5));
  ASSERT_EQ(node_manager.GetFreeNidCount(), init_fcnt - 2);

  fi = MapTester::GetNextFreeNidInList(node_manager);
  ASSERT_EQ(fi->nid, static_cast<nid_t>(5));
  ASSERT_EQ(fi->state, static_cast<int>(NidState::kNidAlloc));

  fs->GetNodeManager().AllocNidFailed(nid);
  fi = MapTester::GetTailFreeNidInList(node_manager);
  ASSERT_EQ(fi->nid, static_cast<nid_t>(5));
  ASSERT_EQ(fi->state, static_cast<int>(NidState::kNidNew));

  FileTester::Unmount(std::move(fs), &bc);
}

TEST(NodeManagerTest, NodePage) {
  std::unique_ptr<Bcache> bc;
  FileTester::MkfsOnFakeDev(&bc);

  std::unique_ptr<F2fs> fs;
  MountOptions options{};
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  FileTester::MountWithOptions(loop.dispatcher(), options, &bc, &fs);

  fbl::RefPtr<VnodeF2fs> root;
  FileTester::CreateRoot(fs.get(), &root);
  fbl::RefPtr<Dir> root_dir = fbl::RefPtr<Dir>::Downcast(std::move(root));

  // Alloc Inode
  fbl::RefPtr<VnodeF2fs> vnode;
  FileTester::VnodeWithoutParent(fs.get(), S_IFREG, vnode);
  ASSERT_EQ(fs->GetNodeManager().NewInodePage(root_dir.get(), vnode.get()), ZX_OK);
  nid_t inode_nid = vnode->Ino();

  DnodeOfData dn;
  NodeManager &node_manager = fs->GetNodeManager();
  uint64_t free_node_cnt = node_manager.GetFreeNidCount();

  // Inode block
  //   |- direct node
  //   |- direct node
  //   |- indirect node
  //   |            `- direct node
  //   |- indirect node
  //   |            `- direct node
  //   `- double indirect node
  //                `- indirect node
  //                      `- direct node

  // Check inode (level 0)
  NodeManager::SetNewDnode(dn, vnode.get(), nullptr, nullptr, 0);
  const pgoff_t direct_index = 1;

  ASSERT_EQ(fs->GetNodeManager().GetDnodeOfData(dn, direct_index, 0), ZX_OK);
  MapTester::CheckDnodeOfData(&dn, inode_nid, direct_index, true);
  F2fsPutDnode(&dn);

  ASSERT_EQ(fs->GetNodeManager().GetDnodeOfData(dn, direct_index, kRdOnlyNode), ZX_OK);
  MapTester::CheckDnodeOfData(&dn, inode_nid, direct_index, true);
  F2fsPutDnode(&dn);
  ASSERT_EQ(node_manager.GetFreeNidCount(), free_node_cnt);
  inode_nid += 1;

  // Check direct node (level 1)
  pgoff_t indirect_index_lv1 = direct_index + kAddrsPerInode;

  ASSERT_EQ(fs->GetNodeManager().GetDnodeOfData(dn, indirect_index_lv1, 0), ZX_OK);
  MapTester::CheckDnodeOfData(&dn, inode_nid, indirect_index_lv1, false);
  F2fsPutDnode(&dn);

  ASSERT_EQ(fs->GetNodeManager().GetDnodeOfData(dn, indirect_index_lv1, kRdOnlyNode), ZX_OK);
  MapTester::CheckDnodeOfData(&dn, inode_nid, indirect_index_lv1, false);
  F2fsPutDnode(&dn);
  ASSERT_EQ(node_manager.GetFreeNidCount(), free_node_cnt -= 1);
  inode_nid += 2;

  // Check indirect node (level 2)
  const pgoff_t direct_blks = kAddrsPerBlock;
  pgoff_t indirect_index_lv2 = indirect_index_lv1 + direct_blks * 2;

  ASSERT_EQ(fs->GetNodeManager().GetDnodeOfData(dn, indirect_index_lv2, 0), ZX_OK);
  MapTester::CheckDnodeOfData(&dn, inode_nid, indirect_index_lv2, false);
  F2fsPutDnode(&dn);

  ASSERT_EQ(fs->GetNodeManager().GetDnodeOfData(dn, indirect_index_lv2, kRdOnlyNode), ZX_OK);
  MapTester::CheckDnodeOfData(&dn, inode_nid, indirect_index_lv2, false);
  F2fsPutDnode(&dn);
  ASSERT_EQ(node_manager.GetFreeNidCount(), free_node_cnt -= 2);
  inode_nid += 2;

  // Check second indirect node (level 2)
  const pgoff_t indirect_blks = kAddrsPerBlock * kNidsPerBlock;

  ASSERT_EQ(fs->GetNodeManager().GetDnodeOfData(dn, indirect_index_lv2 + indirect_blks, 0), ZX_OK);
  MapTester::CheckDnodeOfData(&dn, inode_nid, indirect_index_lv2 + indirect_blks, false);
  F2fsPutDnode(&dn);

  ASSERT_EQ(
      fs->GetNodeManager().GetDnodeOfData(dn, indirect_index_lv2 + indirect_blks, kRdOnlyNode),
      ZX_OK);
  MapTester::CheckDnodeOfData(&dn, inode_nid, indirect_index_lv2 + indirect_blks, false);
  F2fsPutDnode(&dn);
  ASSERT_EQ(node_manager.GetFreeNidCount(), free_node_cnt -= 2);
  inode_nid += 3;

  // Check double indirect node (level 3)
  pgoff_t indirect_index_lv3 = indirect_index_lv2 + indirect_blks * 2;

  ASSERT_EQ(fs->GetNodeManager().GetDnodeOfData(dn, indirect_index_lv3, 0), ZX_OK);
  MapTester::CheckDnodeOfData(&dn, inode_nid, indirect_index_lv3, false);
  F2fsPutDnode(&dn);

  ASSERT_EQ(fs->GetNodeManager().GetDnodeOfData(dn, indirect_index_lv3, kRdOnlyNode), ZX_OK);
  MapTester::CheckDnodeOfData(&dn, inode_nid, indirect_index_lv3, false);
  F2fsPutDnode(&dn);
  ASSERT_EQ(node_manager.GetFreeNidCount(), free_node_cnt -= 3);

  vnode->SetBlocks(1);

  ASSERT_EQ(vnode->Close(), ZX_OK);
  vnode.reset();
  ASSERT_EQ(root_dir->Close(), ZX_OK);
  root_dir.reset();

  FileTester::Unmount(std::move(fs), &bc);
}

TEST(NodeManagerTest, NodePageExceptionCase) {
  std::unique_ptr<Bcache> bc;
  FileTester::MkfsOnFakeDev(&bc);

  std::unique_ptr<F2fs> fs;
  MountOptions options{};
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  FileTester::MountWithOptions(loop.dispatcher(), options, &bc, &fs);

  fbl::RefPtr<VnodeF2fs> root;
  FileTester::CreateRoot(fs.get(), &root);
  fbl::RefPtr<Dir> root_dir = fbl::RefPtr<Dir>::Downcast(std::move(root));

  // Alloc Inode
  fbl::RefPtr<VnodeF2fs> vnode;
  FileTester::VnodeWithoutParent(fs.get(), S_IFREG, vnode);
  ASSERT_EQ(fs->GetNodeManager().NewInodePage(root_dir.get(), vnode.get()), ZX_OK);

  DnodeOfData dn;
  NodeManager &node_manager = fs->GetNodeManager();
  SbInfo &sbi = fs->GetSbInfo();

  // Inode block
  //   |- direct node
  //   |- direct node
  //   |- indirect node
  //   |            `- direct node
  //   |- indirect node
  //   |            `- direct node
  //   `- double indirect node
  //                `- indirect node
  //                      `- direct node

  // Check inode (level 0)
  NodeManager::SetNewDnode(dn, vnode.get(), nullptr, nullptr, 0);

  const pgoff_t direct_index = 1;
  const pgoff_t direct_blks = kAddrsPerBlock;
  const pgoff_t indirect_blks = kAddrsPerBlock * kNidsPerBlock;
  const pgoff_t indirect_index_lv1 = direct_index + kAddrsPerInode;
  const pgoff_t indirect_index_lv2 = indirect_index_lv1 + direct_blks * 2;
  const pgoff_t indirect_index_lv3 = indirect_index_lv2 + indirect_blks * 2;

  // Check invalid page offset exception case
  pgoff_t indirect_index_invalid_lv4 = indirect_index_lv3 + indirect_blks * kNidsPerBlock;
  ASSERT_EQ(fs->GetNodeManager().GetDnodeOfData(dn, indirect_index_invalid_lv4, 0),
            ZX_ERR_NOT_FOUND);

  // Check invalid address
  ASSERT_EQ(fs->GetNodeManager().GetDnodeOfData(dn, indirect_index_lv3 + 1, 0), ZX_OK);

  // fault injection for ReadNodePage()
  MapTester::SetCachedNatEntryBlockAddress(node_manager, dn.nid, kNullAddr);
  F2fsPutDnode(&dn);

  ASSERT_EQ(fs->GetNodeManager().GetDnodeOfData(dn, indirect_index_lv3 + 1, 0), ZX_ERR_NOT_FOUND);

  // Check IncValidNodeCount() exception case
  block_t tmp_total_valid_block_count = sbi.total_valid_block_count;
  sbi.total_valid_block_count = sbi.user_block_count;
  ASSERT_EQ(fs->GetNodeManager().GetDnodeOfData(dn, indirect_index_lv1 + direct_blks, 0),
            ZX_ERR_NO_SPACE);
  sbi.total_valid_block_count = tmp_total_valid_block_count;
  F2fsPutDnode(&dn);

  block_t tmp_total_valid_node_count = sbi.total_valid_node_count;
  sbi.total_valid_node_count = sbi.total_node_count;
  ASSERT_EQ(fs->GetNodeManager().GetDnodeOfData(dn, indirect_index_lv1 + direct_blks, 0),
            ZX_ERR_NO_SPACE);
  sbi.total_valid_node_count = tmp_total_valid_node_count;
  F2fsPutDnode(&dn);

  // Check NewNodePage() exception case
  fbl::RefPtr<VnodeF2fs> test_vnode;
  FileTester::VnodeWithoutParent(fs.get(), S_IFREG, test_vnode);

  test_vnode->SetFlag(InodeInfoFlag::kNoAlloc);
  ASSERT_EQ(fs->GetNodeManager().NewInodePage(root_dir.get(), test_vnode.get()),
            ZX_ERR_ACCESS_DENIED);
  test_vnode->ClearFlag(InodeInfoFlag::kNoAlloc);

  tmp_total_valid_block_count = sbi.total_valid_block_count;
  sbi.total_valid_block_count = sbi.user_block_count;
  ASSERT_EQ(fs->GetNodeManager().NewInodePage(root_dir.get(), test_vnode.get()), ZX_ERR_NO_SPACE);
  ASSERT_EQ(test_vnode->Close(), ZX_OK);
  test_vnode.reset();
  sbi.total_valid_block_count = tmp_total_valid_block_count;

  // Check ReadNodePage() exception case
  ASSERT_EQ(fs->GetNodeManager().GetDnodeOfData(dn, indirect_index_lv1 + 1, 0), ZX_OK);

  // fault injection for ReadNodePage()
  MapTester::SetCachedNatEntryBlockAddress(node_manager, dn.nid, kNewAddr);
  F2fsPutDnode(&dn);

  Page *page = GrabCachePage(nullptr, NodeIno(&sbi), dn.nid);
  ASSERT_EQ(fs->GetNodeManager().ReadNodePage(*page, dn.nid, kReadSync), ZX_ERR_INVALID_ARGS);
  F2fsPutPage(page, 0);

  vnode->SetBlocks(1);

  ASSERT_EQ(vnode->Close(), ZX_OK);
  vnode.reset();
  ASSERT_EQ(root_dir->Close(), ZX_OK);
  root_dir.reset();

  FileTester::Unmount(std::move(fs), &bc);
}

TEST(NodeManagerTest, TruncateDoubleIndirect) {
  std::unique_ptr<Bcache> bc;
  FileTester::MkfsOnFakeDev(&bc);

  std::unique_ptr<F2fs> fs;
  MountOptions options{};
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  FileTester::MountWithOptions(loop.dispatcher(), options, &bc, &fs);

  fbl::RefPtr<VnodeF2fs> root;
  FileTester::CreateRoot(fs.get(), &root);
  fbl::RefPtr<Dir> root_dir = fbl::RefPtr<Dir>::Downcast(std::move(root));

  // Alloc Inode
  fbl::RefPtr<VnodeF2fs> vnode;
  FileTester::VnodeWithoutParent(fs.get(), S_IFREG, vnode);
  ASSERT_EQ(fs->GetNodeManager().NewInodePage(root_dir.get(), vnode.get()), ZX_OK);

  DnodeOfData dn;
  SbInfo &sbi = fs->GetSbInfo();

  // Inode block
  //   |- direct node
  //   |- direct node
  //   |- indirect node
  //   |            `- direct node
  //   |- indirect node
  //   |            `- direct node
  //   `- double indirect node
  //                `- indirect node
  //                      `- direct node

  // Alloc a double indirect node (level 3)
  const pgoff_t direct_blks = kAddrsPerBlock;
  const pgoff_t indirect_blks = kAddrsPerBlock * kNidsPerBlock;
  const pgoff_t direct_index = kAddrsPerInode + 1;
  const pgoff_t indirect_index = direct_index + direct_blks * 2;
  const pgoff_t double_indirect_index = indirect_index + indirect_blks * 2;
  const uint32_t inode_cnt = 2;

  ASSERT_EQ(sbi.total_valid_inode_count, inode_cnt);
  ASSERT_EQ(sbi.total_valid_node_count, inode_cnt);

  std::vector<nid_t> nids;
  NodeManager &node_manager = fs->GetNodeManager();
  uint64_t initial_free_nid_cnt = node_manager.GetFreeNidCount();

  // Alloc a direct node at double_indirect_index
  NodeManager::SetNewDnode(dn, vnode.get(), nullptr, nullptr, 0);
  ASSERT_EQ(fs->GetNodeManager().GetDnodeOfData(dn, double_indirect_index, 0), ZX_OK);
  nids.push_back(dn.nid);
  F2fsPutDnode(&dn);

  // # of alloc nodes = 1 double indirect + 1 indirect + 1 direct
  uint32_t alloc_node_cnt = 3;
  uint32_t node_cnt = inode_cnt + alloc_node_cnt;

  // alloc_dnode cnt should be one
  ASSERT_EQ(nids.size(), 1UL);
  ASSERT_EQ(sbi.total_valid_inode_count, inode_cnt);
  ASSERT_EQ(sbi.total_valid_node_count, node_cnt);

  // Truncate double the indirect node
  ASSERT_EQ(fs->GetNodeManager().TruncateInodeBlocks(*vnode, double_indirect_index), ZX_OK);
  node_cnt = inode_cnt;
  ASSERT_EQ(sbi.total_valid_node_count, node_cnt);

  MapTester::RemoveTruncatedNode(node_manager, nids);
  ASSERT_EQ(nids.size(), 0UL);

  ASSERT_EQ(node_manager.GetFreeNidCount(), initial_free_nid_cnt - alloc_node_cnt);
  fs->WriteCheckpoint(false, false);
  // After checkpoint, we can reuse the removed nodes
  ASSERT_EQ(node_manager.GetFreeNidCount(), initial_free_nid_cnt);

  ASSERT_EQ(vnode->Close(), ZX_OK);
  vnode.reset();
  ASSERT_EQ(root_dir->Close(), ZX_OK);
  root_dir.reset();

  FileTester::Unmount(std::move(fs), &bc);
}

TEST(NodeManagerTest, TruncateIndirect) {
  std::unique_ptr<Bcache> bc;
  FileTester::MkfsOnFakeDev(&bc);

  std::unique_ptr<F2fs> fs;
  MountOptions options{};
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  FileTester::MountWithOptions(loop.dispatcher(), options, &bc, &fs);

  fbl::RefPtr<VnodeF2fs> root;
  FileTester::CreateRoot(fs.get(), &root);
  fbl::RefPtr<Dir> root_dir = fbl::RefPtr<Dir>::Downcast(std::move(root));

  // Alloc Inode
  fbl::RefPtr<VnodeF2fs> vnode;
  FileTester::VnodeWithoutParent(fs.get(), S_IFREG, vnode);
  ASSERT_EQ(fs->GetNodeManager().NewInodePage(root_dir.get(), vnode.get()), ZX_OK);

  DnodeOfData dn;
  SbInfo &sbi = fs->GetSbInfo();

  // Inode block
  //   |- direct node
  //   |- direct node
  //   |- indirect node
  //   |            `- direct node
  // Fill indirect node (level 2)
  const pgoff_t direct_blks = kAddrsPerBlock;
  const pgoff_t direct_index = kAddrsPerInode + 1;
  const pgoff_t indirect_index = direct_index + direct_blks * 2;
  const uint32_t inode_cnt = 2;

  ASSERT_EQ(sbi.total_valid_inode_count, inode_cnt);
  ASSERT_EQ(sbi.total_valid_node_count, inode_cnt);

  std::vector<nid_t> nids;
  NodeManager &node_manager = fs->GetNodeManager();
  uint64_t initial_free_nid_cnt = node_manager.GetFreeNidCount();

  NodeManager::SetNewDnode(dn, vnode.get(), nullptr, nullptr, 0);
  // Start from kAddrsPerInode to alloc new dnodes
  for (pgoff_t i = kAddrsPerInode; i <= indirect_index; i += kAddrsPerBlock) {
    ASSERT_EQ(fs->GetNodeManager().GetDnodeOfData(dn, i, 0), ZX_OK);
    nids.push_back(dn.nid);
    F2fsPutDnode(&dn);
  }

  uint32_t indirect_node_cnt = 1;
  uint32_t direct_node_cnt = 3;
  uint32_t node_cnt = inode_cnt + direct_node_cnt + indirect_node_cnt;
  uint32_t alloc_node_cnt = indirect_node_cnt + direct_node_cnt;

  ASSERT_EQ(nids.size(), direct_node_cnt);
  ASSERT_EQ(sbi.total_valid_inode_count, inode_cnt);
  ASSERT_EQ(sbi.total_valid_node_count, node_cnt);

  // Truncate indirect nodes
  ASSERT_EQ(fs->GetNodeManager().TruncateInodeBlocks(*vnode, indirect_index), ZX_OK);
  indirect_node_cnt--;
  direct_node_cnt--;
  node_cnt = inode_cnt + direct_node_cnt + indirect_node_cnt;
  ASSERT_EQ(sbi.total_valid_node_count, node_cnt);

  MapTester::RemoveTruncatedNode(node_manager, nids);

  ASSERT_EQ(nids.size(), direct_node_cnt);

  // Truncate direct nodes
  ASSERT_EQ(fs->GetNodeManager().TruncateInodeBlocks(*vnode, direct_index), ZX_OK);
  direct_node_cnt -= 2;
  node_cnt = inode_cnt + direct_node_cnt + indirect_node_cnt;
  ASSERT_EQ(sbi.total_valid_node_count, node_cnt);

  MapTester::RemoveTruncatedNode(node_manager, nids);
  ASSERT_EQ(nids.size(), direct_node_cnt);

  ASSERT_EQ(sbi.total_valid_inode_count, inode_cnt);

  ASSERT_EQ(node_manager.GetFreeNidCount(), initial_free_nid_cnt - alloc_node_cnt);
  fs->WriteCheckpoint(false, false);
  // After checkpoint, we can reuse the removed nodes
  ASSERT_EQ(node_manager.GetFreeNidCount(), initial_free_nid_cnt);

  ASSERT_EQ(vnode->Close(), ZX_OK);
  vnode.reset();
  ASSERT_EQ(root_dir->Close(), ZX_OK);
  root_dir.reset();

  FileTester::Unmount(std::move(fs), &bc);
}

TEST(NodeMgrTest, TruncateExceptionCase) {
  std::unique_ptr<Bcache> bc;
  FileTester::MkfsOnFakeDev(&bc);

  std::unique_ptr<F2fs> fs;
  MountOptions options{};
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  FileTester::MountWithOptions(loop.dispatcher(), options, &bc, &fs);

  fbl::RefPtr<VnodeF2fs> root;
  FileTester::CreateRoot(fs.get(), &root);
  fbl::RefPtr<Dir> root_dir = fbl::RefPtr<Dir>::Downcast(std::move(root));

  // Alloc Inode
  fbl::RefPtr<VnodeF2fs> vnode;
  FileTester::VnodeWithoutParent(fs.get(), S_IFREG, vnode);
  ASSERT_EQ(fs->GetNodeManager().NewInodePage(root_dir.get(), vnode.get()), ZX_OK);

  DnodeOfData dn;
  SbInfo &sbi = fs->GetSbInfo();

  // Inode block
  //   |- direct node
  //   |- direct node

  // Fill direct node (level 1)
  const pgoff_t direct_index = kAddrsPerInode + 1;
  const uint32_t inode_cnt = 2;

  ASSERT_EQ(sbi.total_valid_inode_count, inode_cnt);
  ASSERT_EQ(sbi.total_valid_node_count, inode_cnt);

  std::vector<nid_t> nids;
  NodeManager &node_manager = fs->GetNodeManager();
  uint64_t initial_free_nid_cnt = node_manager.GetFreeNidCount();

  NodeManager::SetNewDnode(dn, vnode.get(), nullptr, nullptr, 0);
  // Start from kAddrsPerInode to alloc new dnodes
  for (pgoff_t i = kAddrsPerInode; i <= direct_index; i += kAddrsPerBlock) {
    ASSERT_EQ(fs->GetNodeManager().GetDnodeOfData(dn, i, 0), ZX_OK);
    nids.push_back(dn.nid);
    F2fsPutDnode(&dn);
  }

  uint32_t direct_node_cnt = 1;
  uint32_t node_cnt = inode_cnt + direct_node_cnt;
  uint32_t alloc_node_cnt = direct_node_cnt;

  ASSERT_EQ(nids.size(), direct_node_cnt);
  ASSERT_EQ(sbi.total_valid_inode_count, inode_cnt);
  ASSERT_EQ(sbi.total_valid_node_count, node_cnt);

  block_t temp_block_address = 0;

  // Check exception case truncation of invalid address
  ASSERT_EQ(fs->GetNodeManager().GetDnodeOfData(dn, direct_index, 0), ZX_OK);
  temp_block_address = MapTester::GetCachedNatEntryBlockAddress(node_manager, dn.nid);

  // Enable fault injection for ReadNodePage()
  MapTester::SetCachedNatEntryBlockAddress(node_manager, dn.nid, kNullAddr);
  F2fsPutDnode(&dn);

  // Truncate fault injected dnode
  ASSERT_EQ(fs->GetNodeManager().TruncateInodeBlocks(*vnode, direct_index), ZX_OK);

  // Disable fault injected dnode
  MapTester::SetCachedNatEntryBlockAddress(node_manager, dn.nid, temp_block_address);

  // Retry truncate
  ASSERT_EQ(fs->GetNodeManager().TruncateInodeBlocks(*vnode, direct_index), ZX_OK);
  direct_node_cnt--;
  node_cnt = inode_cnt + direct_node_cnt;
  ASSERT_EQ(sbi.total_valid_node_count, node_cnt);

  MapTester::RemoveTruncatedNode(node_manager, nids);
  ASSERT_EQ(nids.size(), 0UL);

  ASSERT_EQ(sbi.total_valid_inode_count, inode_cnt);

  ASSERT_EQ(node_manager.GetFreeNidCount(), initial_free_nid_cnt - alloc_node_cnt);
  fs->WriteCheckpoint(false, false);
  // After checkpoint, we can reuse the removed nodes
  ASSERT_EQ(node_manager.GetFreeNidCount(), initial_free_nid_cnt);

  ASSERT_EQ(vnode->Close(), ZX_OK);
  vnode.reset();
  ASSERT_EQ(root_dir->Close(), ZX_OK);
  root_dir.reset();

  FileTester::Unmount(std::move(fs), &bc);
}

TEST(NodeMgrTest, NodeFooter) {
  std::unique_ptr<Bcache> bc;
  FileTester::MkfsOnFakeDev(&bc);

  std::unique_ptr<F2fs> fs;
  MountOptions options{};
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  FileTester::MountWithOptions(loop.dispatcher(), options, &bc, &fs);

  fbl::RefPtr<VnodeF2fs> root;
  FileTester::CreateRoot(fs.get(), &root);
  fbl::RefPtr<Dir> root_dir = fbl::RefPtr<Dir>::Downcast(std::move(root));

  // Alloc Inode
  fbl::RefPtr<VnodeF2fs> vnode;
  FileTester::VnodeWithoutParent(fs.get(), S_IFREG, vnode);
  ASSERT_EQ(fs->GetNodeManager().NewInodePage(root_dir.get(), vnode.get()), ZX_OK);
  nid_t inode_nid = vnode->Ino();

  DnodeOfData dn;
  SbInfo &sbi = fs->GetSbInfo();

  NodeManager::SetNewDnode(dn, vnode.get(), nullptr, nullptr, 0);
  const pgoff_t direct_index = 1;

  ASSERT_EQ(fs->GetNodeManager().GetDnodeOfData(dn, direct_index, 0), ZX_OK);
  MapTester::CheckDnodeOfData(&dn, inode_nid, direct_index, true);

  Page *page = GrabCachePage(nullptr, MetaIno(&sbi), direct_index);

  // Check CopyNodeFooter()
  NodeManager::CopyNodeFooter(*page, *dn.node_page);

  ASSERT_EQ(NodeManager::InoOfNode(*page), vnode->Ino());
  ASSERT_EQ(NodeManager::InoOfNode(*page), NodeManager::InoOfNode(*dn.node_page));
  ASSERT_EQ(NodeManager::NidOfNode(*page), NodeManager::NidOfNode(*dn.node_page));
  ASSERT_EQ(NodeManager::OfsOfNode(*page), NodeManager::OfsOfNode(*dn.node_page));
  ASSERT_EQ(NodeManager::CpverOfNode(*page), NodeManager::CpverOfNode(*dn.node_page));
  ASSERT_EQ(NodeManager::NextBlkaddrOfNode(*page), NodeManager::NextBlkaddrOfNode(*dn.node_page));

  // Check footer.flag
  ASSERT_EQ(NodeManager::IsFsyncDnode(*page), NodeManager::IsFsyncDnode(*dn.node_page));
  ASSERT_EQ(NodeManager::IsFsyncDnode(*page), 0);
  NodeManager::SetFsyncMark(*page, 1);
  ASSERT_EQ(NodeManager::IsFsyncDnode(*page), 0x1 << static_cast<int>(BitShift::kFsyncBitShift));
  NodeManager::SetFsyncMark(*page, 0);
  ASSERT_EQ(NodeManager::IsFsyncDnode(*page), 0);

  ASSERT_EQ(NodeManager::IsDentDnode(*page), NodeManager::IsDentDnode(*dn.node_page));
  ASSERT_EQ(NodeManager::IsDentDnode(*page), 0);
  NodeManager::SetDentryMark(*page, 0);
  ASSERT_EQ(NodeManager::IsDentDnode(*page), 0);
  NodeManager::SetDentryMark(*page, 1);
  ASSERT_EQ(NodeManager::IsDentDnode(*page), 0x1 << static_cast<int>(BitShift::kDentBitShift));
  int mark = !fs->GetNodeManager().IsCheckpointedNode(NodeManager::InoOfNode(*page));
  NodeManager::SetDentryMark(*page, mark);
  ASSERT_EQ(NodeManager::IsDentDnode(*page), 0x1 << static_cast<int>(BitShift::kDentBitShift));

  F2fsPutPage(page, 0);
  F2fsPutDnode(&dn);

  ASSERT_EQ(vnode->Close(), ZX_OK);
  vnode.reset();
  ASSERT_EQ(root_dir->Close(), ZX_OK);
  root_dir.reset();

  FileTester::Unmount(std::move(fs), &bc);
}

}  // namespace
}  // namespace f2fs
