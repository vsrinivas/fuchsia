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

TEST(NodeMgrTest, NatCache) {
  std::unique_ptr<Bcache> bc;
  unittest_lib::MkfsOnFakeDev(&bc);

  std::unique_ptr<F2fs> fs;
  MountOptions options{};
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  unittest_lib::MountWithOptions(loop.dispatcher(), options, &bc, &fs);

  SbInfo &sbi = fs->GetSbInfo();
  NmInfo *nm_i = GetNmInfo(&sbi);

  fbl::RefPtr<VnodeF2fs> root;
  unittest_lib::CreateRoot(fs.get(), &root);
  fbl::RefPtr<Dir> root_dir = fbl::RefPtr<Dir>::Downcast(std::move(root));

  // 1. Check NAT cache is empty
  // TODO(jaeyoon): Change list NAT cache to tree NAT cache when tree NAT cache is enabled
  ASSERT_EQ(list_length(&nm_i->nat_entries), static_cast<size_t>(1));  // root inode
  ASSERT_TRUE(list_is_empty(&nm_i->dirty_nat_entries));

  list_node_t *next_node, *node = list_next(&nm_i->nat_entries, &nm_i->nat_entries);
  NatEntry *e = containerof(node, NatEntry, list);
  ASSERT_EQ(e->ni.nid, static_cast<nid_t>(3));  // root inode

  // 2. Check NAT entry is cached in dirty NAT entries list
  std::vector<fbl::RefPtr<VnodeF2fs>> vnodes;
  std::vector<uint32_t> inos;

  // Fill NAT cache
  unittest_lib::CreateChildren(fs.get(), vnodes, inos, root_dir, "NATCache_", kMaxNodeCnt);
  ASSERT_EQ(vnodes.size(), kMaxNodeCnt);
  ASSERT_EQ(inos.size(), kMaxNodeCnt);

  ASSERT_EQ(list_length(&nm_i->nat_entries), static_cast<size_t>(0));
  ASSERT_EQ(list_length(&nm_i->dirty_nat_entries), static_cast<size_t>(kMaxNodeCnt + 1));
  ASSERT_EQ(nm_i->nat_cnt, static_cast<uint32_t>(kMaxNodeCnt + 1));

  // Lookup NAT cache
  for (auto ino : inos) {
    NodeInfo ni;
    ASSERT_TRUE(unittest_lib::IsCachedNat(nm_i, ino));
    fs->Nodemgr().GetNodeInfo(ino, &ni);
    ASSERT_EQ(ni.nid, ino);
  }

  // Move dirty entries to clean entries
  fs->Nodemgr().FlushNatEntries();

  // 3. Check NAT entry is cached in clean NAT entries list
  ASSERT_EQ(list_length(&nm_i->nat_entries), static_cast<size_t>(kMaxNodeCnt + 1));
  ASSERT_EQ(list_length(&nm_i->dirty_nat_entries), static_cast<size_t>(0));
  ASSERT_EQ(nm_i->nat_cnt, static_cast<uint32_t>(kMaxNodeCnt + 1));

  // Lookup NAT cache
  for (auto ino : inos) {
    NodeInfo ni;
    ASSERT_TRUE(unittest_lib::IsCachedNat(nm_i, ino));
    fs->Nodemgr().GetNodeInfo(ino, &ni);
    ASSERT_EQ(ni.nid, ino);
  }

  // 4. Check NAT entry is in the summary journal, not in the NAT Cache
  // Flush NAT cache
  list_for_every_safe(&nm_i->nat_entries, node, next_node) {
    e = containerof(node, NatEntry, list);
    list_delete(&e->list);
    nm_i->nat_cnt--;
    delete e;
  }
  ASSERT_EQ(nm_i->nat_cnt, static_cast<uint32_t>(0));

  CursegInfo *curseg = SegMgr::CURSEG_I(&sbi, CursegType::kCursegHotData);  // NAT Journal
  SummaryBlock *sum = curseg->sum_blk;

  ASSERT_EQ(list_length(&nm_i->nat_entries), static_cast<size_t>(0));
  ASSERT_EQ(list_length(&nm_i->dirty_nat_entries), static_cast<size_t>(0));
  ASSERT_EQ(NatsInCursum(sum), static_cast<int>(kMaxNodeCnt + 1));

  // Lookup NAT journal
  for (auto ino : inos) {
    NodeInfo ni;
    ASSERT_FALSE(unittest_lib::IsCachedNat(nm_i, ino));
    fs->Nodemgr().GetNodeInfo(ino, &ni);
    ASSERT_EQ(ni.nid, ino);
  }

  // 5. Check NAT cache miss and journal miss
  std::vector<uint32_t> journal_inos;

  // Fill NAT cache with journal size -2
  // Root inode NAT(nid=4) is duplicated in cache and journal, so we need to keep two empty NAT
  // entries
  unittest_lib::CreateChildren(fs.get(), vnodes, journal_inos, root_dir, "NATJournal_",
                               kNatJournalEntries - kMaxNodeCnt - 2);
  ASSERT_EQ(vnodes.size(), kNatJournalEntries - 2);
  ASSERT_EQ(inos.size() + journal_inos.size(), kNatJournalEntries - 2);

  // Fill NAT journal
  fs->Nodemgr().FlushNatEntries();
  ASSERT_EQ(NatsInCursum(sum), static_cast<int>(kNatJournalEntries - 1));

  // Fill NAT cache over journal size
  unittest_lib::CreateChildren(fs.get(), vnodes, journal_inos, root_dir, "NATJournalFlush_", 2);
  ASSERT_EQ(vnodes.size(), kNatJournalEntries);
  ASSERT_EQ(inos.size() + journal_inos.size(), kNatJournalEntries);

  // Flush NAT journal
  fs->Nodemgr().FlushNatEntries();
  ASSERT_EQ(NatsInCursum(sum), static_cast<int>(0));

  // Flush NAT cache
  list_for_every_safe(&nm_i->nat_entries, node, next_node) {
    e = containerof(node, NatEntry, list);
    list_delete(&e->list);
    nm_i->nat_cnt--;
    delete e;
  }

  // Check NAT cache empty
  ASSERT_EQ(list_length(&nm_i->nat_entries), static_cast<size_t>(0));
  ASSERT_EQ(list_length(&nm_i->dirty_nat_entries), static_cast<size_t>(0));
  ASSERT_EQ(nm_i->nat_cnt, static_cast<uint32_t>(0));

  // Read NAT block
  for (auto ino : inos) {
    NodeInfo ni;
    ASSERT_FALSE(unittest_lib::IsCachedNat(nm_i, ino));
    fs->Nodemgr().GetNodeInfo(ino, &ni);
    ASSERT_EQ(ni.nid, ino);
  }

  // Check NAT cache
  ASSERT_EQ(list_length(&nm_i->nat_entries), static_cast<size_t>(10));
  ASSERT_EQ(list_length(&nm_i->dirty_nat_entries), static_cast<size_t>(0));
  ASSERT_EQ(nm_i->nat_cnt, static_cast<uint32_t>(10));

  for (auto &vnode_refptr : vnodes) {
    ASSERT_EQ(vnode_refptr->Close(), ZX_OK);
    vnode_refptr.reset();
  }

  ASSERT_EQ(root_dir->Close(), ZX_OK);
  root_dir.reset();

  unittest_lib::Unmount(std::move(fs), &bc);
}

TEST(NodeMgrTest, FreeNid) {
  std::unique_ptr<Bcache> bc;
  unittest_lib::MkfsOnFakeDev(&bc);

  std::unique_ptr<F2fs> fs;
  MountOptions options{};
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  unittest_lib::MountWithOptions(loop.dispatcher(), options, &bc, &fs);

  SbInfo &sbi = fs->GetSbInfo();
  NmInfo *nm_i = GetNmInfo(&sbi);

  ASSERT_EQ(nm_i->init_scan_nid, static_cast<nid_t>(4));

  list_node_t *this_list;
  FreeNid *fi = nullptr;
  nid_t nid = nm_i->init_scan_nid;
  uint64_t init_fcnt = nm_i->fcnt;

  // Check initial free list (BuildFreeNids)
  list_for_every(&nm_i->free_nid_list, this_list) {
    fi = containerof(this_list, FreeNid, list);
    ASSERT_EQ(fi->nid, nid);
    ASSERT_EQ(fi->state, static_cast<int>(NidState::kNidNew));
    nid++;
  }
  ASSERT_EQ(nid, nm_i->next_scan_nid);

  // Alloc Done
  fs->Nodemgr().AllocNid(&nid);
  ASSERT_EQ(nid, static_cast<nid_t>(4));
  ASSERT_EQ(nm_i->fcnt, init_fcnt - 1);

  fi = containerof(nm_i->free_nid_list.next, FreeNid, list);
  ASSERT_EQ(fi->nid, static_cast<nid_t>(4));
  ASSERT_EQ(fi->state, static_cast<int>(NidState::kNidAlloc));

  fs->Nodemgr().AllocNidDone(nid);
  fi = containerof(nm_i->free_nid_list.next, FreeNid, list);
  ASSERT_EQ(fi->nid, static_cast<nid_t>(5));
  ASSERT_EQ(fi->state, static_cast<int>(NidState::kNidNew));

  // Alloc Failed
  fs->Nodemgr().AllocNid(&nid);
  ASSERT_EQ(nid, static_cast<nid_t>(5));
  ASSERT_EQ(nm_i->fcnt, init_fcnt - 2);

  fi = containerof(nm_i->free_nid_list.next, FreeNid, list);
  ASSERT_EQ(fi->nid, static_cast<nid_t>(5));
  ASSERT_EQ(fi->state, static_cast<int>(NidState::kNidAlloc));

  fs->Nodemgr().AllocNidFailed(nid);
  list_node_t *free_list_tail = list_peek_tail(&nm_i->free_nid_list);
  fi = containerof(free_list_tail, FreeNid, list);
  ASSERT_EQ(fi->nid, static_cast<nid_t>(5));
  ASSERT_EQ(fi->state, static_cast<int>(NidState::kNidNew));

  unittest_lib::Unmount(std::move(fs), &bc);
}

TEST(NodeMgrTest, NodePage) {
  std::unique_ptr<Bcache> bc;
  unittest_lib::MkfsOnFakeDev(&bc);

  std::unique_ptr<F2fs> fs;
  MountOptions options{};
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  unittest_lib::MountWithOptions(loop.dispatcher(), options, &bc, &fs);

  fbl::RefPtr<VnodeF2fs> root;
  unittest_lib::CreateRoot(fs.get(), &root);
  fbl::RefPtr<Dir> root_dir = fbl::RefPtr<Dir>::Downcast(std::move(root));

  // Alloc Inode
  fbl::RefPtr<VnodeF2fs> vnode;
  unittest_lib::VnodeWithoutParent(fs.get(), S_IFREG, vnode);
  ASSERT_EQ(fs->Nodemgr().NewInodePage(root_dir.get(), vnode.get()), ZX_OK);
  nid_t inode_nid = vnode->Ino();

  DnodeOfData dn;
  SbInfo &sbi = fs->GetSbInfo();
  NmInfo *nm_i = GetNmInfo(&sbi);
  uint64_t free_node_cnt = nm_i->fcnt;

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
  SetNewDnode(&dn, vnode.get(), nullptr, nullptr, 0);
  const pgoff_t direct_index = 1;

  ASSERT_EQ(fs->Nodemgr().GetDnodeOfData(&dn, direct_index, 0), ZX_OK);
  unittest_lib::CheckDnodeOfData(&dn, inode_nid, direct_index, true);
  F2fsPutDnode(&dn);

  ASSERT_EQ(fs->Nodemgr().GetDnodeOfData(&dn, direct_index, kRdOnlyNode), ZX_OK);
  unittest_lib::CheckDnodeOfData(&dn, inode_nid, direct_index, true);
  F2fsPutDnode(&dn);
  ASSERT_EQ(nm_i->fcnt, free_node_cnt);
  inode_nid += 1;

  // Check direct node (level 1)
  pgoff_t indirect_index_lv1 = direct_index + kAddrsPerInode;

  ASSERT_EQ(fs->Nodemgr().GetDnodeOfData(&dn, indirect_index_lv1, 0), ZX_OK);
  unittest_lib::CheckDnodeOfData(&dn, inode_nid, indirect_index_lv1, false);
  F2fsPutDnode(&dn);

  ASSERT_EQ(fs->Nodemgr().GetDnodeOfData(&dn, indirect_index_lv1, kRdOnlyNode), ZX_OK);
  unittest_lib::CheckDnodeOfData(&dn, inode_nid, indirect_index_lv1, false);
  F2fsPutDnode(&dn);
  ASSERT_EQ(nm_i->fcnt, free_node_cnt -= 1);
  inode_nid += 2;

  // Check indirect node (level 2)
  const pgoff_t direct_blks = kAddrsPerBlock;
  pgoff_t indirect_index_lv2 = indirect_index_lv1 + direct_blks * 2;

  ASSERT_EQ(fs->Nodemgr().GetDnodeOfData(&dn, indirect_index_lv2, 0), ZX_OK);
  unittest_lib::CheckDnodeOfData(&dn, inode_nid, indirect_index_lv2, false);
  F2fsPutDnode(&dn);

  ASSERT_EQ(fs->Nodemgr().GetDnodeOfData(&dn, indirect_index_lv2, kRdOnlyNode), ZX_OK);
  unittest_lib::CheckDnodeOfData(&dn, inode_nid, indirect_index_lv2, false);
  F2fsPutDnode(&dn);
  ASSERT_EQ(nm_i->fcnt, free_node_cnt -= 2);
  inode_nid += 3;

  // Check double indirect node (level 3)
  const pgoff_t indirect_blks = kAddrsPerBlock * kNidsPerBlock;
  pgoff_t indirect_index_lv3 = indirect_index_lv2 + indirect_blks * 2;

  ASSERT_EQ(fs->Nodemgr().GetDnodeOfData(&dn, indirect_index_lv3, 0), ZX_OK);
  unittest_lib::CheckDnodeOfData(&dn, inode_nid, indirect_index_lv3, false);
  F2fsPutDnode(&dn);

  ASSERT_EQ(fs->Nodemgr().GetDnodeOfData(&dn, indirect_index_lv3, kRdOnlyNode), ZX_OK);
  unittest_lib::CheckDnodeOfData(&dn, inode_nid, indirect_index_lv3, false);
  F2fsPutDnode(&dn);
  ASSERT_EQ(nm_i->fcnt, free_node_cnt -= 3);

  vnode->SetBlocks(1);

  ASSERT_EQ(vnode->Close(), ZX_OK);
  vnode.reset();
  ASSERT_EQ(root_dir->Close(), ZX_OK);
  root_dir.reset();

  unittest_lib::Unmount(std::move(fs), &bc);
}

// TODO(fxbug.dev/83831) This test takes >1 minute to run on ASAN in some configurations such as ARM
// on the buildbot. We should configure the test so there is a longer timeout and re-enable.
#if !__has_feature(address_sanitizer)
TEST(NodeMgrTest, Truncate) {
  std::unique_ptr<Bcache> bc;
  unittest_lib::MkfsOnFakeDev(&bc);

  std::unique_ptr<F2fs> fs;
  MountOptions options{};
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  unittest_lib::MountWithOptions(loop.dispatcher(), options, &bc, &fs);

  fbl::RefPtr<VnodeF2fs> root;
  unittest_lib::CreateRoot(fs.get(), &root);
  fbl::RefPtr<Dir> root_dir = fbl::RefPtr<Dir>::Downcast(std::move(root));

  // Alloc Inode
  fbl::RefPtr<VnodeF2fs> vnode;
  unittest_lib::VnodeWithoutParent(fs.get(), S_IFREG, vnode);
  ASSERT_EQ(fs->Nodemgr().NewInodePage(root_dir.get(), vnode.get()), ZX_OK);

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

  // Fill double indirect node (level 3)
  const pgoff_t direct_blks = kAddrsPerBlock;
  const pgoff_t indirect_blks = kAddrsPerBlock * kNidsPerBlock;
  const pgoff_t direct_index = kAddrsPerInode + 1;
  const pgoff_t indirect_index = direct_index + direct_blks * 2;
  const pgoff_t double_indirect_index = indirect_index + indirect_blks * 2;
  const uint32_t inode_cnt = 2;

  ASSERT_EQ(sbi.total_valid_inode_count, inode_cnt);
  ASSERT_EQ(sbi.total_valid_node_count, inode_cnt);

  std::vector<nid_t> nids;
  NmInfo *nm_i = GetNmInfo(&sbi);
  uint64_t initial_free_nid_cnt = nm_i->fcnt;

  // Fill Dnodes
  SetNewDnode(&dn, vnode.get(), nullptr, nullptr, 0);
  for (pgoff_t i = 0; i < double_indirect_index; i++) {
    ASSERT_EQ(fs->Nodemgr().GetDnodeOfData(&dn, i, 0), ZX_OK);
    if (dn.ofs_in_node == 0) {
      nids.push_back(dn.nid);
    }
    F2fsPutDnode(&dn);
  }

  uint32_t indirect_node_cnt = 3;
  uint32_t direct_node_cnt = direct_blks * 2 + 3;
  uint32_t double_indirect_node_cnt = 1;
  uint32_t node_cnt = inode_cnt + direct_node_cnt + indirect_node_cnt + double_indirect_node_cnt;
  uint32_t alloc_dnode_cnt = node_cnt - inode_cnt;

  ASSERT_EQ(nids.size(), node_cnt - indirect_node_cnt - double_indirect_node_cnt -
                             1);  // Direct node - root inode
  ASSERT_EQ(sbi.total_valid_inode_count, inode_cnt);
  ASSERT_EQ(sbi.total_valid_node_count, node_cnt);

  // Truncate double indirect nodes
  ASSERT_EQ(fs->Nodemgr().TruncateInodeBlocks(vnode.get(), double_indirect_index), ZX_OK);
  indirect_node_cnt--;
  direct_node_cnt--;
  node_cnt = inode_cnt + direct_node_cnt + indirect_node_cnt;
  ASSERT_EQ(sbi.total_valid_node_count, node_cnt);

  unittest_lib::RemoveTruncatedNode(nm_i, nids);
  ASSERT_EQ(nids.size(), node_cnt - indirect_node_cnt - 1);  // Direct node - root inode

  // Truncate indirect nodes
  ASSERT_EQ(fs->Nodemgr().TruncateInodeBlocks(vnode.get(), indirect_index), ZX_OK);
  indirect_node_cnt -= 2;
  direct_node_cnt -= direct_blks * 2;
  node_cnt = inode_cnt + direct_node_cnt;
  ASSERT_EQ(sbi.total_valid_node_count, node_cnt);

  unittest_lib::RemoveTruncatedNode(nm_i, nids);
  ASSERT_EQ(nids.size(), node_cnt - 1);  // Direct node - root inode

  // Valid nodes:
  // Inode block (nid = 4)
  //   |- direct node (nid = 5)
  //   |- direct node (nid = 6)
  ASSERT_EQ(nids[0], static_cast<nid_t>(4));
  ASSERT_EQ(nids[1], static_cast<nid_t>(5));
  ASSERT_EQ(nids[2], static_cast<nid_t>(6));

  // Truncate direct nodes
  ASSERT_EQ(fs->Nodemgr().TruncateInodeBlocks(vnode.get(), direct_index), ZX_OK);
  node_cnt = inode_cnt;
  ASSERT_EQ(sbi.total_valid_node_count, node_cnt);

  unittest_lib::RemoveTruncatedNode(nm_i, nids);
  ASSERT_EQ(nids.size(), node_cnt - 1);  // Direct node - root inode

  // Valid nodes:
  // Inode block (nid = 4)
  ASSERT_EQ(nids[0], static_cast<nid_t>(4));
  ASSERT_EQ(sbi.total_valid_inode_count, inode_cnt);

  ASSERT_EQ(nm_i->fcnt, initial_free_nid_cnt - alloc_dnode_cnt);
  fs->WriteCheckpoint(false, false);
  ASSERT_EQ(nm_i->fcnt, initial_free_nid_cnt);

  ASSERT_EQ(vnode->Close(), ZX_OK);
  vnode.reset();
  ASSERT_EQ(root_dir->Close(), ZX_OK);
  root_dir.reset();

  unittest_lib::Unmount(std::move(fs), &bc);
}
#endif

}  // namespace
}  // namespace f2fs
