// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/f2fs/f2fs.h"

namespace f2fs {

NodeManager::NodeManager(F2fs *fs) : fs_(fs), superblock_info_(&fs_->GetSuperblockInfo()) {}

NodeManager::NodeManager(SuperblockInfo *sbi) : superblock_info_(sbi) {}

void NodeManager::SetNatCacheDirty(NatEntry &ne) {
  ZX_ASSERT(clean_nat_list_.erase(ne) != nullptr);
  dirty_nat_list_.push_back(&ne);
}

void NodeManager::ClearNatCacheDirty(NatEntry &ne) {
  ZX_ASSERT(dirty_nat_list_.erase(ne) != nullptr);
  clean_nat_list_.push_back(&ne);
}

void NodeManager::NodeInfoFromRawNat(NodeInfo &ni, RawNatEntry &raw_ne) {
  ni.ino = LeToCpu(raw_ne.ino);
  ni.blk_addr = LeToCpu(raw_ne.block_addr);
  ni.version = raw_ne.version;
}

bool NodeManager::IncValidNodeCount(VnodeF2fs *vnode, uint32_t count) {
  block_t valid_block_count;
  uint32_t ValidNodeCount;

  std::lock_guard stat_lock(GetSuperblockInfo().GetStatLock());

  valid_block_count = GetSuperblockInfo().GetTotalValidBlockCount() + static_cast<block_t>(count);
  GetSuperblockInfo().SetAllocValidBlockCount(GetSuperblockInfo().GetAllocValidBlockCount() +
                                              static_cast<block_t>(count));
  ValidNodeCount = GetSuperblockInfo().GetTotalValidNodeCount() + count;

  if (valid_block_count > GetSuperblockInfo().GetUserBlockCount()) {
    return false;
  }

  if (ValidNodeCount > GetSuperblockInfo().GetTotalNodeCount()) {
    return false;
  }

  if (vnode)
    vnode->IncBlocks(count);
  GetSuperblockInfo().SetTotalValidNodeCount(ValidNodeCount);
  GetSuperblockInfo().SetTotalValidBlockCount(valid_block_count);

  return true;
}

zx_status_t NodeManager::NextFreeNid(nid_t *nid) {
  if (free_nid_count_ <= 0)
    return ZX_ERR_OUT_OF_RANGE;

  std::lock_guard free_nid_lock(free_nid_list_lock_);
  FreeNid *fnid = containerof(free_nid_list_.next, FreeNid, list);
  *nid = fnid->nid;
  return ZX_OK;
}

void NodeManager::GetNatBitmap(void *out) {
  memcpy(out, nat_bitmap_.get(), nat_bitmap_size_);
  memcpy(nat_prev_bitmap_.get(), nat_bitmap_.get(), nat_bitmap_size_);
}

pgoff_t NodeManager::CurrentNatAddr(nid_t start) {
  pgoff_t block_off;
  pgoff_t block_addr;
  pgoff_t seg_off;

  block_off = NatBlockOffset(start);
  seg_off = block_off >> GetSuperblockInfo().GetLogBlocksPerSeg();

  block_addr = static_cast<pgoff_t>(
      nat_blkaddr_ + (seg_off << GetSuperblockInfo().GetLogBlocksPerSeg() << 1) +
      (block_off & ((1 << GetSuperblockInfo().GetLogBlocksPerSeg()) - 1)));

  if (TestValidBitmap(block_off, nat_bitmap_.get()))
    block_addr += GetSuperblockInfo().GetBlocksPerSeg();

  return block_addr;
}

bool NodeManager::IsUpdatedNatPage(nid_t start) {
  pgoff_t block_off;

  block_off = NatBlockOffset(start);

  return (TestValidBitmap(block_off, nat_bitmap_.get()) ^
          TestValidBitmap(block_off, nat_prev_bitmap_.get()));
}

pgoff_t NodeManager::NextNatAddr(pgoff_t block_addr) {
  block_addr -= nat_blkaddr_;
  if ((block_addr >> GetSuperblockInfo().GetLogBlocksPerSeg()) % 2)
    block_addr -= GetSuperblockInfo().GetBlocksPerSeg();
  else
    block_addr += GetSuperblockInfo().GetBlocksPerSeg();

  return block_addr + nat_blkaddr_;
}

void NodeManager::SetToNextNat(nid_t start_nid) {
  pgoff_t block_off = NatBlockOffset(start_nid);

  if (TestValidBitmap(block_off, nat_bitmap_.get()))
    ClearValidBitmap(block_off, nat_bitmap_.get());
  else
    SetValidBitmap(block_off, nat_bitmap_.get());
}

void NodeManager::FillNodeFooter(Page &page, nid_t nid, nid_t ino, uint32_t ofs, bool reset) {
  Node *rn = static_cast<Node *>(page.GetAddress());
  if (reset)
    memset(rn, 0, sizeof(*rn));
  rn->footer.nid = CpuToLe(nid);
  rn->footer.ino = CpuToLe(ino);
  rn->footer.flag = CpuToLe(ofs << static_cast<int>(BitShift::kOffsetBitShift));
}

void NodeManager::CopyNodeFooter(Page &dst, Page &src) {
  Node *src_rn = static_cast<Node *>(src.GetAddress());
  Node *dst_rn = static_cast<Node *>(dst.GetAddress());
  memcpy(&dst_rn->footer, &src_rn->footer, sizeof(NodeFooter));
}

void NodeManager::FillNodeFooterBlkaddr(Page &page, block_t blkaddr) {
  Checkpoint &ckpt = GetSuperblockInfo().GetCheckpoint();
  Node *rn = static_cast<Node *>(page.GetAddress());
  rn->footer.cp_ver = ckpt.checkpoint_ver;
  rn->footer.next_blkaddr = blkaddr;
}

nid_t NodeManager::InoOfNode(Page &node_page) {
  Node *rn = static_cast<Node *>(node_page.GetAddress());
  return LeToCpu(rn->footer.ino);
}

nid_t NodeManager::NidOfNode(Page &node_page) {
  Node *rn = static_cast<Node *>(node_page.GetAddress());
  return LeToCpu(rn->footer.nid);
}

uint32_t NodeManager::OfsOfNode(Page &node_page) {
  Node *rn = static_cast<Node *>(node_page.GetAddress());
  uint32_t flag = LeToCpu(rn->footer.flag);
  return flag >> static_cast<int>(BitShift::kOffsetBitShift);
}

uint64_t NodeManager::CpverOfNode(Page &node_page) {
  Node *rn = static_cast<Node *>(node_page.GetAddress());
  return LeToCpu(rn->footer.cp_ver);
}

block_t NodeManager::NextBlkaddrOfNode(Page &node_page) {
  Node *rn = static_cast<Node *>(node_page.GetAddress());
  return LeToCpu(rn->footer.next_blkaddr);
}

// f2fs assigns the following node offsets described as (num).
// N = kNidsPerBlock
//
//  Inode block (0)
//    |- direct node (1)
//    |- direct node (2)
//    |- indirect node (3)
//    |            `- direct node (4 => 4 + N - 1)
//    |- indirect node (4 + N)
//    |            `- direct node (5 + N => 5 + 2N - 1)
//    `- double indirect node (5 + 2N)
//                 `- indirect node (6 + 2N)
//                       `- direct node (x(N + 1))
bool NodeManager::IS_DNODE(Page &node_page) {
  uint32_t ofs = OfsOfNode(node_page);
  if (ofs == 3 || ofs == 4 + kNidsPerBlock || ofs == 5 + 2 * kNidsPerBlock)
    return false;
  if (ofs >= 6 + 2 * kNidsPerBlock) {
    ofs -= 6 + 2 * kNidsPerBlock;
    if (static_cast<int64_t>(ofs) % (kNidsPerBlock + 1))
      return false;
  }
  return true;
}

void NodeManager::SetNid(Page &page, int off, nid_t nid, bool is_inode) {
  Node *rn = static_cast<Node *>(page.GetAddress());

  page.WaitOnWriteback();

  if (is_inode) {
    rn->i.i_nid[off - kNodeDir1Block] = CpuToLe(nid);
  } else {
    rn->in.nid[off] = CpuToLe(nid);
  }

  page.SetDirty();
}

nid_t NodeManager::GetNid(Page &page, int off, bool is_inode) {
  Node *rn = static_cast<Node *>(page.GetAddress());
  if (is_inode) {
    return LeToCpu(rn->i.i_nid[off - kNodeDir1Block]);
  }
  return LeToCpu(rn->in.nid[off]);
}

// Coldness identification:
//  - Mark cold files in InodeInfo
//  - Mark cold node blocks in their node footer
//  - Mark cold data pages in page cache
bool NodeManager::IsColdFile(VnodeF2fs &vnode) { return (vnode.IsAdviseSet(FAdvise::kCold) != 0); }

#if 0  // When gc impl, use the cold hint.
int NodeManager::IsColdData(Page *page) {
  // return PageChecked(page);
  return 0;
}

void NodeManager::SetColdData(Page &page) {
  // SetPageChecked(page);
}

void NodeManager::ClearColdData(Page *page) {
  // ClearPageChecked(page);
}
#endif

int NodeManager::IsColdNode(Page &page) {
  Node *rn = static_cast<Node *>(page.GetAddress());
  uint32_t flag = LeToCpu(rn->footer.flag);
  return flag & (0x1 << static_cast<int>(BitShift::kColdBitShift));
}

uint8_t NodeManager::IsFsyncDnode(Page &page) {
  Node *rn = static_cast<Node *>(page.GetAddress());
  uint32_t flag = LeToCpu(rn->footer.flag);
  return flag & (0x1 << static_cast<int>(BitShift::kFsyncBitShift));
}

uint8_t NodeManager::IsDentDnode(Page &page) {
  Node *rn = static_cast<Node *>(page.GetAddress());
  uint32_t flag = LeToCpu(rn->footer.flag);
  return flag & (0x1 << static_cast<int>(BitShift::kDentBitShift));
}

void NodeManager::SetColdNode(VnodeF2fs &vnode, Page &page) {
  Node *rn = static_cast<Node *>(page.GetAddress());
  uint32_t flag = LeToCpu(rn->footer.flag);

  if (vnode.IsDir())
    flag &= ~(0x1 << static_cast<int>(BitShift::kColdBitShift));
  else
    flag |= (0x1 << static_cast<int>(BitShift::kColdBitShift));
  rn->footer.flag = CpuToLe(flag);
}

void NodeManager::SetFsyncMark(Page &page, int mark) {
  Node *rn = static_cast<Node *>(page.GetAddress());
  uint32_t flag = LeToCpu(rn->footer.flag);
  if (mark)
    flag |= (0x1 << static_cast<int>(BitShift::kFsyncBitShift));
  else
    flag &= ~(0x1 << static_cast<int>(BitShift::kFsyncBitShift));
  rn->footer.flag = CpuToLe(flag);
}

void NodeManager::SetDentryMark(Page &page, int mark) {
  Node *rn = static_cast<Node *>(page.GetAddress());
  uint32_t flag = LeToCpu(rn->footer.flag);
  if (mark)
    flag |= (0x1 << static_cast<int>(BitShift::kDentBitShift));
  else
    flag &= ~(0x1 << static_cast<int>(BitShift::kDentBitShift));
  rn->footer.flag = CpuToLe(flag);
}

void NodeManager::DecValidNodeCount(VnodeF2fs *vnode, uint32_t count) {
  std::lock_guard stat_lock(GetSuperblockInfo().GetStatLock());

  ZX_ASSERT(!(GetSuperblockInfo().GetTotalValidBlockCount() < count));
  ZX_ASSERT(!(GetSuperblockInfo().GetTotalValidNodeCount() < count));

  vnode->DecBlocks(count);
  GetSuperblockInfo().SetTotalValidNodeCount(GetSuperblockInfo().GetTotalValidNodeCount() - count);
  GetSuperblockInfo().SetTotalValidBlockCount(GetSuperblockInfo().GetTotalValidBlockCount() -
                                              count);
}

void NodeManager::GetCurrentNatPage(nid_t nid, fbl::RefPtr<Page> *out) {
  pgoff_t index = CurrentNatAddr(nid);
  fs_->GetMetaPage(index, out);
}

void NodeManager::GetNextNatPage(nid_t nid, fbl::RefPtr<Page> *out) {
  fbl::RefPtr<Page> src_page;
  fbl::RefPtr<Page> dst_page;

  pgoff_t src_off = CurrentNatAddr(nid);
  pgoff_t dst_off = NextNatAddr(src_off);

  // get current nat block page with lock
  fs_->GetMetaPage(src_off, &src_page);

  // Dirty src_page means that it is already the new target NAT page
#if 0  // porting needed
  // if (PageDirty(src_page))
#endif
  if (IsUpdatedNatPage(nid)) {
    *out = std::move(src_page);
    return;
  }

  fs_->GrabMetaPage(dst_off, &dst_page);

  memcpy(dst_page->GetAddress(), src_page->GetAddress(), kPageSize);
  dst_page->SetDirty();
  Page::PutPage(std::move(src_page), true);

  SetToNextNat(nid);

  *out = std::move(dst_page);
}

// Readahead NAT pages
void NodeManager::RaNatPages(nid_t nid) {
  for (int i = 0; i < kFreeNidPages; ++i, nid += kNatEntryPerBlock) {
    if (nid >= max_nid_) {
      nid = 0;
    }
    fbl::RefPtr<Page> page;
    pgoff_t index = CurrentNatAddr(nid);
    if (zx_status_t ret = fs_->GetMetaPage(index, &page); ret != ZX_OK) {
      continue;
    }
#if 0  // porting needed
    // page_cache_release(page);
#endif
    Page::PutPage(std::move(page), true);
  }
}

NatEntry *NodeManager::LookupNatCache(nid_t n) {
  if (auto nat_entry = nat_cache_.find(n); nat_entry != nat_cache_.end()) {
    return &(*nat_entry);
  }
  return nullptr;
}

uint32_t NodeManager::GangLookupNatCache(uint32_t nr, NatEntry **out) {
  uint32_t ret = 0;
  for (auto &entry : nat_cache_) {
    out[ret] = &entry;
    if (++ret == nr)
      break;
  }
  return ret;
}

void NodeManager::DelFromNatCache(NatEntry &entry) {
  ZX_ASSERT_MSG(clean_nat_list_.erase(entry) != nullptr, "Cannot find NAT in list(nid = %u)",
                entry.GetNid());
  auto deleted = nat_cache_.erase(entry);
  ZX_ASSERT_MSG(deleted != nullptr, "Cannot find NAT in cache(nid = %u)", entry.GetNid());
  --nat_entries_count_;
}

bool NodeManager::IsCheckpointedNode(nid_t nid) {
  fs::SharedLock nat_lock(nat_tree_lock_);
  NatEntry *ne = LookupNatCache(nid);
  if (ne && !ne->IsCheckpointed()) {
    return false;
  }
  return true;
}

NatEntry *NodeManager::GrabNatEntry(nid_t nid) {
  auto new_entry = std::make_unique<NatEntry>();

  if (!new_entry)
    return nullptr;

  auto entry = new_entry.get();
  entry->SetNid(nid);

  clean_nat_list_.push_back(entry);
  nat_cache_.insert(std::move(new_entry));
  ++nat_entries_count_;
  return entry;
}

void NodeManager::CacheNatEntry(nid_t nid, RawNatEntry &raw_entry) {
  while (true) {
    std::lock_guard lock(nat_tree_lock_);
    NatEntry *entry = LookupNatCache(nid);
    if (!entry) {
      if (entry = GrabNatEntry(nid); !entry) {
        continue;
      }
    }
    entry->SetBlockAddress(LeToCpu(raw_entry.block_addr));
    entry->SetIno(LeToCpu(raw_entry.ino));
    entry->SetVersion(raw_entry.version);
    entry->SetCheckpointed();
    break;
  }
}

void NodeManager::SetNodeAddr(NodeInfo &ni, block_t new_blkaddr) {
  while (true) {
    std::lock_guard nat_lock(nat_tree_lock_);
    NatEntry *entry = LookupNatCache(ni.nid);
    if (!entry) {
      entry = GrabNatEntry(ni.nid);
      if (!entry) {
        continue;
      }
      entry->SetNodeInfo(ni);
      entry->SetCheckpointed();
      ZX_ASSERT(ni.blk_addr != kNewAddr);
    } else if (new_blkaddr == kNewAddr) {
      // when nid is reallocated,
      // previous nat entry can be remained in nat cache.
      // So, reinitialize it with new information.
      entry->SetNodeInfo(ni);
      ZX_ASSERT(ni.blk_addr == kNullAddr);
    }

    if (new_blkaddr == kNewAddr) {
      entry->ClearCheckpointed();
    }

    // sanity check
    ZX_ASSERT(!(entry->GetBlockAddress() != ni.blk_addr));
    ZX_ASSERT(!(entry->GetBlockAddress() == kNullAddr && new_blkaddr == kNullAddr));
    ZX_ASSERT(!(entry->GetBlockAddress() == kNewAddr && new_blkaddr == kNewAddr));
    ZX_ASSERT(!(entry->GetBlockAddress() != kNewAddr && entry->GetBlockAddress() != kNullAddr &&
                new_blkaddr == kNewAddr));

    // increament version no as node is removed
    if (entry->GetBlockAddress() != kNewAddr && new_blkaddr == kNullAddr) {
      uint8_t version = entry->GetVersion();
      entry->SetVersion(IncNodeVersion(version));
    }

    // change address
    entry->SetBlockAddress(new_blkaddr);
    SetNatCacheDirty(*entry);
    break;
  }
}

int NodeManager::TryToFreeNats(int nr_shrink) {
  if (nat_entries_count_ < 2 * kNmWoutThreshold)
    return 0;

  std::lock_guard nat_lock(nat_tree_lock_);
  while (nr_shrink && !clean_nat_list_.is_empty()) {
    NatEntry *cache_entry = &clean_nat_list_.front();
    DelFromNatCache(*cache_entry);
    --nr_shrink;
  }
  return nr_shrink;
}

// This function returns always success
void NodeManager::GetNodeInfo(nid_t nid, NodeInfo &out) {
  CursegInfo *curseg = fs_->GetSegmentManager().CURSEG_I(CursegType::kCursegHotData);
  SummaryBlock *sum = curseg->sum_blk;
  nid_t start_nid = StartNid(nid);
  NatBlock *nat_blk;
  fbl::RefPtr<Page> page;
  RawNatEntry ne;
  int i;

  out.nid = nid;

  {
    // Check nat cache
    fs::SharedLock nat_lock(nat_tree_lock_);
    NatEntry *entry = LookupNatCache(nid);
    if (entry) {
      out.ino = entry->GetIno();
      out.blk_addr = entry->GetBlockAddress();
      out.version = entry->GetVersion();
      return;
    }
  }

  {
    // Check current segment summary
    std::lock_guard curseg_lock(curseg->curseg_mutex);
    i = LookupJournalInCursum(sum, JournalType::kNatJournal, nid, 0);
    if (i >= 0) {
      ne = NatInJournal(sum, i);
      NodeInfoFromRawNat(out, ne);
    }
  }
  if (i < 0) {
    // Fill NodeInfo from nat page
    GetCurrentNatPage(start_nid, &page);
    nat_blk = static_cast<NatBlock *>(page->GetAddress());
    ne = nat_blk->entries[nid - start_nid];

    NodeInfoFromRawNat(out, ne);
    Page::PutPage(std::move(page), true);
  }
  CacheNatEntry(nid, ne);
}

// The maximum depth is four.
// Offset[0] will have raw inode offset.
zx::status<int> NodeManager::GetNodePath(VnodeF2fs &vnode, long block, int (&offset)[4],
                                         uint32_t (&noffset)[4]) {
  const long direct_index = kAddrsPerInode - (vnode.GetExtraISize() / sizeof(uint32_t));
  const long direct_blks = kAddrsPerBlock;
  const long dptrs_per_blk = kNidsPerBlock;
  const long indirect_blks = kAddrsPerBlock * kNidsPerBlock;
  const long dindirect_blks = indirect_blks * kNidsPerBlock;
  int n = 0;
  int level = 0;

  noffset[0] = 0;
  do {
    if (block < direct_index) {
      offset[n++] = static_cast<int>(block);
      level = 0;
      break;
    }
    block -= direct_index;
    if (block < direct_blks) {
      offset[n++] = kNodeDir1Block;
      noffset[n] = 1;
      offset[n++] = static_cast<int>(block);
      level = 1;
      break;
    }
    block -= direct_blks;
    if (block < direct_blks) {
      offset[n++] = kNodeDir2Block;
      noffset[n] = 2;
      offset[n++] = static_cast<int>(block);
      level = 1;
      break;
    }
    block -= direct_blks;
    if (block < indirect_blks) {
      offset[n++] = kNodeInd1Block;
      noffset[n] = 3;
      offset[n++] = static_cast<int>(block / direct_blks);
      noffset[n] = 4 + offset[n - 1];
      offset[n++] = block % direct_blks;
      level = 2;
      break;
    }
    block -= indirect_blks;
    if (block < indirect_blks) {
      offset[n++] = kNodeInd2Block;
      noffset[n] = 4 + dptrs_per_blk;
      offset[n++] = static_cast<int>(block / direct_blks);
      noffset[n] = 5 + dptrs_per_blk + offset[n - 1];
      offset[n++] = block % direct_blks;
      level = 2;
      break;
    }
    block -= indirect_blks;
    if (block < dindirect_blks) {
      offset[n++] = kNodeDIndBlock;
      noffset[n] = 5 + (dptrs_per_blk * 2);
      offset[n++] = static_cast<int>(block / indirect_blks);
      noffset[n] = 6 + (dptrs_per_blk * 2) + offset[n - 1] * (dptrs_per_blk + 1);
      offset[n++] = (block / direct_blks) % dptrs_per_blk;
      noffset[n] = 7 + (dptrs_per_blk * 2) + offset[n - 2] * (dptrs_per_blk + 1) + offset[n - 1];
      offset[n++] = block % direct_blks;
      level = 3;
      break;
    } else {
      return zx::error(ZX_ERR_NOT_FOUND);
    }
  } while (false);
  return zx::ok(level);
}

// Caller should call F2fsPutDnode(dn).
zx_status_t NodeManager::GetDnodeOfData(DnodeOfData &dn, pgoff_t index, bool readonly) {
  fbl::RefPtr<Page> npage[4];
  fbl::RefPtr<Page> parent;
  int offset[4];
  uint32_t noffset[4];
  nid_t nids[4] = {0};

  auto node_path = GetNodePath(*dn.vnode, index, offset, noffset);
  if (node_path.is_error()) {
    return node_path.error_value();
  }

  int level = *node_path;
  nids[0] = dn.vnode->Ino();
  if (zx_status_t err = GetNodePage(nids[0], &npage[0]); err != ZX_OK) {
    return err;
  }

  auto release_pages = fit::defer([&]() {
    dn.inode_page = nullptr;
    dn.node_page = nullptr;
    // Avoid releasing |npage[0]| twice.
    if (parent && parent != npage[0]) {
      Page::PutPage(std::move(parent), true);
    }
    for (auto i = 0; i < 4; ++i) {
      if (npage[i]) {
        Page::PutPage(std::move(npage[i]), true);
      }
    }
  });

  parent = npage[0];
  dn.inode_page = npage[0];
  dn.inode_page_locked = true;

  if (level != 0) {
    nids[1] = GetNid(*parent, offset[0], true);
  }

  // get indirect or direct nodes
  for (int i = 1; i <= level; ++i) {
    if (!nids[i] && !readonly) {
      // alloc new node
      if (!AllocNid(nids[i])) {
        return ZX_ERR_NO_SPACE;
      }

      dn.nid = nids[i];
      if (zx_status_t err = NewNodePage(dn, noffset[i], &npage[i]); err != ZX_OK) {
        AllocNidFailed(nids[i]);
        return err;
      }

      SetNid(*parent, offset[i - 1], nids[i], i == 1);
      AllocNidDone(nids[i]);
    } else if (readonly && i == level && level > 1) {
      // TODO: Read ahead Pages
    }
    if (i == 1) {
      dn.inode_page_locked = false;
      parent->Unlock();
    } else {
      Page::PutPage(std::move(parent), true);
    }
    if (!npage[i]) {
      if (zx_status_t err = GetNodePage(nids[i], &npage[i]); err != ZX_OK) {
        return err;
      }
    }
    if (i < level) {
      parent = std::move(npage[i]);
      nids[i + 1] = GetNid(*parent, offset[i], false);
    }
  }
  dn.nid = nids[level];
  dn.ofs_in_node = offset[level];
  dn.node_page = std::move(npage[level]);
  dn.data_blkaddr = DatablockAddr(dn.node_page.get(), dn.ofs_in_node);
  release_pages.cancel();
  return ZX_OK;
}

void NodeManager::TruncateNode(DnodeOfData &dn) {
  NodeInfo ni;

  GetNodeInfo(dn.nid, ni);
  ZX_ASSERT(ni.blk_addr != kNullAddr);

  if (ni.blk_addr != kNullAddr)
    fs_->GetSegmentManager().InvalidateBlocks(ni.blk_addr);

  // Deallocate node address
  DecValidNodeCount(dn.vnode, 1);
  SetNodeAddr(ni, kNullAddr);

  if (dn.nid == dn.vnode->Ino()) {
    fs_->RemoveOrphanInode(dn.nid);
    fs_->DecValidInodeCount();
  } else {
    SyncInodePage(dn);
  }

  dn.node_page->Invalidate();
  GetSuperblockInfo().SetDirty();

  Page::PutPage(std::move(dn.node_page), true);
}

zx_status_t NodeManager::TruncateDnode(DnodeOfData &dn) {
  fbl::RefPtr<Page> page;

  if (dn.nid == 0)
    return 1;

  // get direct node
  if (zx_status_t err = fs_->GetNodeManager().GetNodePage(dn.nid, &page); err != ZX_OK) {
    if (err == ZX_ERR_NOT_FOUND) {
      err = ZX_OK;
    }
    return err;
  }

  dn.node_page = page;
  dn.ofs_in_node = 0;
  dn.vnode->TruncateDataBlocks(&dn);
  TruncateNode(dn);
  return ZX_OK;
}

zx_status_t NodeManager::TruncateNodes(DnodeOfData &dn, uint32_t nofs, int ofs, int depth) {
  DnodeOfData rdn = dn;
  fbl::RefPtr<Page> page;
  Node *rn;
  nid_t child_nid;
  uint32_t child_nofs;
  int freed = 0;
  int ret;
  zx_status_t err = 0;

  if (dn.nid == 0)
    return kNidsPerBlock + 1;

  err = fs_->GetNodeManager().GetNodePage(dn.nid, &page);
  if (err)
    return err;

  rn = static_cast<Node *>(page->GetAddress());
  if (depth < 3) {
    for (int i = ofs; i < kNidsPerBlock; ++i, ++freed) {
      child_nid = LeToCpu(rn->in.nid[i]);
      if (child_nid == 0)
        continue;
      rdn.nid = child_nid;
      ret = TruncateDnode(rdn);
      if (ret < 0) {
        Page::PutPage(std::move(page), true);
        return ret;
      }
      SetNid(*page, i, 0, false);
    }
  } else {
    child_nofs = nofs + ofs * (kNidsPerBlock + 1) + 1;
    for (int i = ofs; i < kNidsPerBlock; ++i) {
      child_nid = LeToCpu(rn->in.nid[i]);
      if (child_nid == 0) {
        child_nofs += kNidsPerBlock + 1;
        continue;
      }
      rdn.nid = child_nid;
      ret = TruncateNodes(rdn, child_nofs, 0, depth - 1);
      if (ret == (kNidsPerBlock + 1)) {
        SetNid(*page, i, 0, false);
        child_nofs += ret;
      } else if (ret < 0 && ret != ZX_ERR_NOT_FOUND) {
        Page::PutPage(std::move(page), true);
        return ret;
      }
    }
    freed = child_nofs;
  }

  if (!ofs) {
    // remove current indirect node
    dn.node_page = std::move(page);
    TruncateNode(dn);
    ++freed;
  } else {
    Page::PutPage(std::move(page), true);
  }
  return freed;
}

zx_status_t NodeManager::TruncatePartialNodes(DnodeOfData &dn, Inode &ri, int (&offset)[4],
                                              int depth) {
  fbl::RefPtr<Page> pages[2];
  nid_t nid[3];
  nid_t child_nid;
  zx_status_t err = 0;
  int idx = depth - 2;
  auto free_pages = [&]() {
    for (int i = idx; i >= 0; --i) {
      if (pages[i]) {
        Page::PutPage(std::move(pages[i]), true);
      }
    }
  };

  nid[0] = LeToCpu(ri.i_nid[offset[0] - kNodeDir1Block]);
  if (!nid[0])
    return ZX_OK;

  // get indirect nodes in the path
  for (int i = 0; i < idx + 1; ++i) {
    // refernece count'll be increased
    pages[i] = nullptr;
    err = fs_->GetNodeManager().GetNodePage(nid[i], &pages[i]);
    if (err) {
      idx = i - 1;
      free_pages();
      return err;
    }
    nid[i + 1] = GetNid(*pages[i], offset[i + 1], false);
  }

  // free direct nodes linked to a partial indirect node
  for (int i = offset[idx + 1]; i < kNidsPerBlock; ++i) {
    child_nid = GetNid(*pages[idx], i, false);
    if (!child_nid)
      continue;
    dn.nid = child_nid;
    err = TruncateDnode(dn);
    if (err < 0) {
      free_pages();
      return err;
    }
    SetNid(*pages[idx], i, 0, false);
  }

  if (offset[idx + 1] == 0) {
    dn.node_page = std::move(pages[idx]);
    dn.nid = nid[idx];
    TruncateNode(dn);
  } else {
    Page::PutPage(std::move(pages[idx]), true);
  }
  ++offset[idx];
  offset[idx + 1] = 0;
  --idx;
  free_pages();
  return err;
}

// All the block addresses of data and nodes should be nullified.
zx_status_t NodeManager::TruncateInodeBlocks(VnodeF2fs &vnode, pgoff_t from) {
  int cont = 1;
  int level, offset[4];
  uint32_t nofs, noffset[4];
  Node *rn;
  DnodeOfData dn;
  fbl::RefPtr<Page> page;
  zx_status_t err = 0;

  auto node_path = GetNodePath(vnode, from, offset, noffset);
  if (node_path.is_error())
    return node_path.error_value();

  level = *node_path;

  err = GetNodePage(vnode.Ino(), &page);
  if (err)
    return err;

  SetNewDnode(dn, &vnode, page, nullptr, 0);
  page->Unlock();

  rn = static_cast<Node *>(page->GetAddress());
  switch (level) {
    case 0:
    case 1:
      nofs = noffset[1];
      break;
    case 2:
      nofs = noffset[1];
      if (!offset[level - 1]) {
        break;
      }
      err = TruncatePartialNodes(dn, rn->i, offset, level);
      if (err < 0 && err != ZX_ERR_NOT_FOUND) {
        Page::PutPage(std::move(page), false);
        return err;
      }
      nofs += 1 + kNidsPerBlock;
      break;
    case 3:
      nofs = 5 + 2 * kNidsPerBlock;
      if (!offset[level - 1]) {
        break;
      }
      err = TruncatePartialNodes(dn, rn->i, offset, level);
      if (err < 0 && err != ZX_ERR_NOT_FOUND) {
        Page::PutPage(std::move(page), false);
        return err;
      }
      break;
    default:
      ZX_ASSERT(0);
  }

  while (cont) {
    dn.nid = LeToCpu(rn->i.i_nid[offset[0] - kNodeDir1Block]);
    switch (offset[0]) {
      case kNodeDir1Block:
      case kNodeDir2Block:
        err = TruncateDnode(dn);
        break;

      case kNodeInd1Block:
      case kNodeInd2Block:
        err = TruncateNodes(dn, nofs, offset[1], 2);
        break;

      case kNodeDIndBlock:
        err = TruncateNodes(dn, nofs, offset[1], 3);
        cont = 0;
        break;

      default:
        ZX_ASSERT(0);
    }
    if (err < 0 && err != ZX_ERR_NOT_FOUND) {
      Page::PutPage(std::move(page), false);
      return err;
    }
    if (offset[1] == 0 && rn->i.i_nid[offset[0] - kNodeDir1Block]) {
      page->Lock();
      page->WaitOnWriteback();
      rn->i.i_nid[offset[0] - kNodeDir1Block] = 0;
      page->SetDirty();
      page->Unlock();
    }
    offset[1] = 0;
    ++offset[0];
    nofs += err;
  }
  Page::PutPage(std::move(page), false);
  return err > 0 ? 0 : err;
}

zx_status_t NodeManager::RemoveInodePage(VnodeF2fs *vnode) {
  fbl::RefPtr<Page> page;
  nid_t ino = vnode->Ino();
  DnodeOfData dn;
  zx_status_t err = 0;

  err = GetNodePage(ino, &page);
  if (err) {
    return err;
  }

  if (nid_t nid = vnode->GetXattrNid(); nid > 0) {
    fbl::RefPtr<Page> node_page;
    err = GetNodePage(nid, &node_page);

    if (err) {
      return err;
    }

    vnode->ClearXattrNid();

    SetNewDnode(dn, vnode, page, node_page, nid);
    dn.inode_page_locked = true;
    TruncateNode(dn);
  }
  if (vnode->GetBlocks() == 1) {
    SetNewDnode(dn, vnode, page, page, ino);
    // internally call Page::PutPage() w/ dn.node_page
    TruncateNode(dn);
  } else if (vnode->GetBlocks() == 0) {
    NodeInfo ni;
    GetNodeInfo(vnode->Ino(), ni);

    ZX_ASSERT(ni.blk_addr == kNullAddr);
    Page::PutPage(std::move(page), true);
  } else {
    ZX_ASSERT(0);
  }
  return ZX_OK;
}

zx_status_t NodeManager::NewInodePage(Dir *parent, VnodeF2fs *child) {
  fbl::RefPtr<Page> page;
  DnodeOfData dn;

  // allocate inode page for new inode
  SetNewDnode(dn, child, nullptr, nullptr, child->Ino());
  if (zx_status_t ret = NewNodePage(dn, 0, &page); ret != ZX_OK) {
    return ret;
  }
  parent->InitDentInode(child, page.get());

  Page::PutPage(std::move(page), true);
  return ZX_OK;
}

zx_status_t NodeManager::NewNodePage(DnodeOfData &dn, uint32_t ofs, fbl::RefPtr<Page> *out) {
  NodeInfo old_ni, new_ni;

  if (dn.vnode->TestFlag(InodeInfoFlag::kNoAlloc)) {
    return ZX_ERR_ACCESS_DENIED;
  }

  if (zx_status_t ret = fs_->GetNodeVnode().GrabCachePage(dn.nid, out); ret != ZX_OK) {
    return ZX_ERR_NO_MEMORY;
  }

  GetNodeInfo(dn.nid, old_ni);

  (*out)->SetUptodate();
  FillNodeFooter(**out, dn.nid, dn.vnode->Ino(), ofs, true);

  // Reinitialize old_ni with new node page
  ZX_ASSERT(old_ni.blk_addr == kNullAddr);
  new_ni = old_ni;
  new_ni.ino = dn.vnode->Ino();

  if (!IncValidNodeCount(dn.vnode, 1)) {
    (*out)->ClearUptodate();
    Page::PutPage(std::move(*out), true);
    fs_->GetInspectTree().OnOutOfSpace();
    return ZX_ERR_NO_SPACE;
  }
  SetNodeAddr(new_ni, kNewAddr);

  dn.node_page = *out;
  SyncInodePage(dn);

  (*out)->SetDirty();
  SetColdNode(*dn.vnode, **out);
  if (ofs == 0)
    fs_->IncValidInodeCount();

  return ZX_OK;
}

zx_status_t NodeManager::ReadNodePage(fbl::RefPtr<Page> page, nid_t nid, int type) {
  NodeInfo ni;

  GetNodeInfo(nid, ni);

  if (ni.blk_addr == kNullAddr)
    return ZX_ERR_NOT_FOUND;

  return fs_->MakeOperation(storage::OperationType::kRead, std::move(page), ni.blk_addr,
                            PageType::kNode);
}

#if 0  // porting needed
// TODO: Readahead a node page
void NodeManager::RaNodePage(nid_t nid) {
  // TODO: IMPL Read ahead
}
#endif

zx_status_t NodeManager::GetNodePage(nid_t nid, fbl::RefPtr<Page> *out) {
  if (zx_status_t ret = fs_->GetNodeVnode().GrabCachePage(nid, out); ret != ZX_OK) {
    return ret;
  }
  if (zx_status_t ret = ReadNodePage(*out, nid, kReadSync); ret != ZX_OK) {
    Page::PutPage(std::move(*out), true);
    return ret;
  }

  ZX_ASSERT(nid == NidOfNode(**out));
#if 0  // porting needed
  // mark_page_accessed(page);
#endif
  return ZX_OK;
}

#if 0  // porting needed
// Return a locked page for the desired node page.
// And, readahead kMaxRaNode number of node pages.
Page *NodeManager::GetNodePageRa(Page *parent, int start) {
  // TODO: IMPL Read ahead
  return nullptr;
}
#endif

void NodeManager::SyncInodePage(DnodeOfData &dn) {
  if (!dn.vnode->GetNlink()) {
    return;
  }

  dn.vnode->MarkInodeDirty();
  if (IsInode(*dn.node_page) || dn.inode_page == dn.node_page) {
    dn.vnode->UpdateInode(dn.node_page.get());
  } else if (dn.inode_page) {
    if (!dn.inode_page_locked) {
      dn.inode_page->Lock();
    }
    dn.vnode->UpdateInode(dn.inode_page.get());
    if (!dn.inode_page_locked) {
      dn.inode_page->Unlock();
    }
  } else {
    dn.vnode->WriteInode(false);
  }
}

pgoff_t NodeManager::SyncNodePages(WritebackOperation &operation) {
  if (superblock_info_->GetPageCount(CountType::kDirtyNodes) == 0 && !operation.bReleasePages) {
    return 0;
  }
  if (zx_status_t status = fs_->GetVCache().ForDirtyVnodesIf(
          [this](fbl::RefPtr<VnodeF2fs> &vnode) {
            if (!vnode->ShouldFlush()) {
              ZX_ASSERT(fs_->GetVCache().RemoveDirty(vnode.get()) == ZX_OK);
              return ZX_ERR_NEXT;
            }
            ZX_ASSERT(vnode->WriteInode(false) == ZX_OK);
            ZX_ASSERT(fs_->GetVCache().RemoveDirty(vnode.get()) == ZX_OK);
            ZX_ASSERT(vnode->ClearDirty() == true);
            return ZX_OK;
          },
          [](fbl::RefPtr<VnodeF2fs> &vnode) {
            if (vnode->GetDirtyPageCount()) {
              return ZX_ERR_NEXT;
            }
            return ZX_OK;
          });
      status != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to flush dirty vnodes ";
    return 0;
  }
  // TODO: Consider ordered writeback
  return fs_->GetNodeVnode().Writeback(operation);

#if 0  // porting needed
  // SuperblockInfo &superblock_info = GetSuperblockInfo();
  // //address_space *mapping = superblock_info.node_inode->i_mapping;
  // pgoff_t index, end;
  // // TODO: IMPL
  // //pagevec pvec;
  // int step = ino ? 2 : 0;
  // int nwritten = 0, wrote = 0;

  // // TODO: IMPL
  // //pagevec_init(&pvec, 0);

  // next_step:
  // 	index = 0;
  // 	end = LONG_MAX;

  // 	while (index <= end) {
  // 		int nr_pages;
  //  TODO: IMPL
  // nr_pages = pagevec_lookup_tag(&pvec, mapping, &index,
  // 		PAGECACHE_TAG_DIRTY,
  // 		min(end - index, (pgoff_t)PAGEVEC_SIZE-1) + 1);
  // if (nr_pages == 0)
  // 	break;

  // 		for (int i = 0; i < nr_pages; ++i) {
  // 			page *page = pvec.pages[i];

  // 			/*
  // 			 * flushing sequence with step:
  // 			 * 0. indirect nodes
  // 			 * 1. dentry dnodes
  // 			 * 2. file dnodes
  // 			 */
  // 			if (step == 0 && IS_DNODE(page))
  // 				continue;
  // 			if (step == 1 && (!IS_DNODE(page) ||
  // 						IsColdNode(page)))
  // 				continue;
  // 			if (step == 2 && (!IS_DNODE(page) ||
  // 						!IsColdNode(page)))
  // 				continue;

  // 			/*
  // 			 * If an fsync mode,
  // 			 * we should not skip writing node pages.
  // 			 */
  // 			if (ino && InoOfNode(page) == ino)
  // 				lock_page(page);
  // 			else if (!trylock_page(page))
  // 				continue;

  // 			if (unlikely(page->mapping != mapping)) {
  // continue_unlock:
  // 				unlock_page(page);
  // 				continue;
  // 			}
  // 			if (ino && InoOfNode(page) != ino)
  // 				goto continue_unlock;

  // 			if (!PageDirty(page)) {
  // 				/* someone wrote it for us */
  // 				goto continue_unlock;
  // 			}

  // 			if (!ClearPageDirtyForIo(page))
  // 				goto continue_unlock;

  // 			/* called by fsync() */
  // 			if (ino && IS_DNODE(page)) {
  // 				int mark = !IsCheckpointedNode(superblock_info, ino);
  // 				SetFsyncMark(page, 1);
  // 				if (IsInode(page))
  // 					SetDentryMark(page, mark);
  // 				++nwritten;
  // 			} else {
  // 				SetFyncMark(page, 0);
  // 				SetDentryMark(page, 0);
  // 			}
  // 			mapping->a_ops->writepage(page, wbc);
  // 			++wrote;

  // 			if (--wbc->nr_to_write == 0)
  // 				break;
  // 		}
  // 		pagevec_release(&pvec);
  // 		cond_resched();

  // 		if (wbc->nr_to_write == 0) {
  // 			step = 2;
  // 			break;
  // 		}
  // 	}

  // 	if (step < 2) {
  // 		++step;
  // 		goto next_step;
  // 	}

  // 	if (wrote)
  // 		f2fs_submit_bio(superblock_info, NODE, wbc->sync_mode == WB_SYNC_ALL);

  //	return nwritten;
#endif
}

zx_status_t NodeManager::F2fsWriteNodePage(fbl::RefPtr<Page> page, bool is_reclaim) {
#if 0  // porting needed
  // 	if (wbc->for_reclaim) {
  // 		superblock_info.DecreasePageCount(CountType::kDirtyNodes);
  // 		++wbc->pages_skipped;
  //		// set_page_dirty(page);
  //		FlushDirtyNodePage(fs_, page);
  // 		return kAopWritepageActivate;
  // 	}
#endif
  page->WaitOnWriteback();
  if (page->ClearDirtyForIo(true)) {
    page->SetWriteback();
    fs::SharedLock rlock(GetSuperblockInfo().GetFsLock(LockType::kNodeOp));
    // get old block addr of this node page
    nid_t nid = NidOfNode(*page);
    ZX_ASSERT(page->GetIndex() == nid);

    NodeInfo ni;
    GetNodeInfo(nid, ni);
    // This page is already truncated
    if (ni.blk_addr == kNullAddr) {
      return ZX_ERR_NOT_FOUND;
    }

    block_t new_addr;
    // insert node offset
    fs_->GetSegmentManager().WriteNodePage(std::move(page), nid, ni.blk_addr, &new_addr);
    SetNodeAddr(ni, new_addr);
  }
  return ZX_OK;
}

#if 0  // porting needed
int NodeManager::F2fsWriteNodePages(struct address_space *mapping, WritebackControl *wbc) {
  // struct SuperblockInfo *superblock_info = F2FS_SB(mapping->host->i_sb);
  // struct block_device *bdev = superblock_info->sb->s_bdev;
  // long nr_to_write = wbc->nr_to_write;

  // if (wbc->for_kupdate)
  // 	return 0;

  // if (superblock_info->GetPageCount(CountType::kDirtyNodes) == 0)
  // 	return 0;

  // if (try_to_free_nats(superblock_info, kNatEntryPerBlock)) {
  // 	write_checkpoint(superblock_info, false, false);
  // 	return 0;
  // }

  // /* if mounting is failed, skip writing node pages */
  // wbc->nr_to_write = bio_get_nr_vecs(bdev);
  // sync_node_pages(superblock_info, 0, wbc);
  // wbc->nr_to_write = nr_to_write -
  // 	(bio_get_nr_vecs(bdev) - wbc->nr_to_write);
  // return 0;
  return 0;
}
#endif

FreeNid *NodeManager::LookupFreeNidList(nid_t n) {
  list_node_t *this_list;
  FreeNid *i = nullptr;
  list_for_every(&free_nid_list_, this_list) {
    i = containerof(this_list, FreeNid, list);
    if (i->nid == n)
      break;
    i = nullptr;
  }
  return i;
}

void NodeManager::DelFromFreeNidList(FreeNid *i) {
  list_delete(&i->list);
  delete i;
}

int NodeManager::AddFreeNid(nid_t nid) {
  FreeNid *i;

  if (free_nid_count_ > 2 * kMaxFreeNids)
    return 0;
  do {
    i = new FreeNid;
    sleep(0);
  } while (!i);
  i->nid = nid;
  i->state = static_cast<int>(NidState::kNidNew);

  std::lock_guard free_nid_lock(free_nid_list_lock_);
  if (LookupFreeNidList(nid)) {
    delete i;
    return 0;
  }
  list_add_tail(&free_nid_list_, &i->list);
  ++free_nid_count_;
  return 1;
}

void NodeManager::RemoveFreeNid(nid_t nid) {
  FreeNid *i;
  std::lock_guard free_nid_lock(free_nid_list_lock_);
  i = LookupFreeNidList(nid);
  if (i && i->state == static_cast<int>(NidState::kNidNew)) {
    DelFromFreeNidList(i);
    --free_nid_count_;
  }
}

int NodeManager::ScanNatPage(Page &nat_page, nid_t start_nid) {
  NatBlock *nat_blk = static_cast<NatBlock *>(nat_page.GetAddress());
  block_t blk_addr;
  int fcnt = 0;

  // 0 nid should not be used
  if (start_nid == 0)
    ++start_nid;

  for (uint32_t i = start_nid % kNatEntryPerBlock; i < kNatEntryPerBlock; ++i, ++start_nid) {
    blk_addr = LeToCpu(nat_blk->entries[i].block_addr);
    ZX_ASSERT(blk_addr != kNewAddr);
    if (blk_addr == kNullAddr)
      fcnt += AddFreeNid(start_nid);
  }
  return fcnt;
}

void NodeManager::BuildFreeNids() {
  FreeNid *fnid, *next_fnid;
  CursegInfo *curseg = fs_->GetSegmentManager().CURSEG_I(CursegType::kCursegHotData);
  SummaryBlock *sum = curseg->sum_blk;
  nid_t nid = 0;
  bool is_cycled = false;
  uint64_t fcnt = 0;

  nid = next_scan_nid_;
  init_scan_nid_ = nid;

  RaNatPages(nid);

  while (true) {
    fbl::RefPtr<Page> page;
    GetCurrentNatPage(nid, &page);

    fcnt += ScanNatPage(*page, nid);
    Page::PutPage(std::move(page), 1);

    nid += (kNatEntryPerBlock - (nid % kNatEntryPerBlock));

    if (nid >= max_nid_) {
      nid = 0;
      is_cycled = true;
    }
    if (fcnt > kMaxFreeNids)
      break;
    if (is_cycled && init_scan_nid_ <= nid)
      break;
  }

  next_scan_nid_ = nid;

  {
    // find free nids from current sum_pages
    std::lock_guard curseg_lock(curseg->curseg_mutex);
    for (int i = 0; i < NatsInCursum(sum); ++i) {
      block_t addr = LeToCpu(NatInJournal(sum, i).block_addr);
      nid = LeToCpu(NidInJournal(sum, i));
      if (addr == kNullAddr) {
        AddFreeNid(nid);
      } else {
        RemoveFreeNid(nid);
      }
    }
  }

  // remove the free nids from current allocated nids
  list_for_every_entry_safe (&free_nid_list_, fnid, next_fnid, FreeNid, list) {
    fs::SharedLock nat_lock(nat_tree_lock_);
    NatEntry *entry = LookupNatCache(fnid->nid);
    if (entry && entry->GetBlockAddress() != kNullAddr) {
      RemoveFreeNid(fnid->nid);
    }
  }
}

// If this function returns success, caller can obtain a new nid
// from second parameter of this function.
// The returned nid could be used ino as well as nid when inode is created.
bool NodeManager::AllocNid(nid_t &out) {
  FreeNid *i = nullptr;
  list_node_t *this_list;
  do {
    {
      std::lock_guard lock(build_lock_);
      if (!free_nid_count_) {
        // scan NAT in order to build free nid list
        BuildFreeNids();
        if (!free_nid_count_) {
          fs_->GetInspectTree().OnOutOfSpace();
          return false;
        }
      }
    }
    // We check fcnt again since previous check is racy as
    // we didn't hold free_nid_list_lock. So other thread
    // could consume all of free nids.
  } while (!free_nid_count_);

  std::lock_guard lock(free_nid_list_lock_);
  ZX_ASSERT(!list_is_empty(&free_nid_list_));

  list_for_every(&free_nid_list_, this_list) {
    i = containerof(this_list, FreeNid, list);
    if (i->state == static_cast<int>(NidState::kNidNew))
      break;
  }

  ZX_ASSERT(i->state == static_cast<int>(NidState::kNidNew));
  out = i->nid;
  i->state = static_cast<int>(NidState::kNidAlloc);
  --free_nid_count_;
  return true;
}

// alloc_nid() should be called prior to this function.
void NodeManager::AllocNidDone(nid_t nid) {
  FreeNid *i;

  std::lock_guard free_nid_lock(free_nid_list_lock_);
  i = LookupFreeNidList(nid);
  if (i) {
    ZX_ASSERT(i->state == static_cast<int>(NidState::kNidAlloc));
    DelFromFreeNidList(i);
  }
}

// alloc_nid() should be called prior to this function.
void NodeManager::AllocNidFailed(nid_t nid) {
  AllocNidDone(nid);
  AddFreeNid(nid);
}

void NodeManager::RecoverNodePage(fbl::RefPtr<Page> page, Summary &sum, NodeInfo &ni,
                                  block_t new_blkaddr) {
  fs_->GetSegmentManager().RewriteNodePage(page, &sum, ni.blk_addr, new_blkaddr);
  SetNodeAddr(ni, new_blkaddr);
  page->Invalidate();
  // TODO: Remove it when impl. recovery.
  ZX_ASSERT(0);
}

zx_status_t NodeManager::RecoverInodePage(Page &page) {
  Node *src, *dst;
  nid_t ino = InoOfNode(page);
  NodeInfo old_ni, new_ni;
  fbl::RefPtr<Page> ipage;

  if (zx_status_t ret = fs_->GetNodeVnode().GrabCachePage(ino, &ipage); ret != ZX_OK) {
    return ret;
  }

  // Should not use this inode  from free nid list
  RemoveFreeNid(ino);

  GetNodeInfo(ino, old_ni);

  ipage->SetUptodate();
  FillNodeFooter(*ipage, ino, ino, 0, true);

  src = static_cast<Node *>(page.GetAddress());
  dst = static_cast<Node *>(ipage->GetAddress());

  memcpy(dst, src, reinterpret_cast<uint64_t>(&src->i.i_ext) - reinterpret_cast<uint64_t>(&src->i));
  dst->i.i_size = 0;
  dst->i.i_blocks = 1;
  dst->i.i_links = 1;
  dst->i.i_xattr_nid = 0;

  new_ni = old_ni;
  new_ni.ino = ino;

  SetNodeAddr(new_ni, kNewAddr);
  fs_->IncValidInodeCount();

  Page::PutPage(std::move(ipage), true);
  return ZX_OK;
}

zx_status_t NodeManager::RestoreNodeSummary(uint32_t segno, SummaryBlock &sum) {
  int last_offset = GetSuperblockInfo().GetBlocksPerSeg();
  block_t addr = fs_->GetSegmentManager().StartBlock(segno);
  Summary *sum_entry = &sum.entries[0];

  for (int i = 0; i < last_offset; ++i, ++sum_entry, ++addr) {
    fbl::RefPtr<Page> page;
    if (zx_status_t ret = fs_->GetMetaPage(addr, &page); ret != ZX_OK) {
      return ret;
    }

    Node *rn = static_cast<Node *>(page->GetAddress());
    sum_entry->nid = rn->footer.nid;
    sum_entry->version = 0;
    sum_entry->ofs_in_node = 0;

    page->Invalidate();
    Page::PutPage(std::move(page), true);
  }
  return ZX_OK;
}

bool NodeManager::FlushNatsInJournal() {
  CursegInfo *curseg = fs_->GetSegmentManager().CURSEG_I(CursegType::kCursegHotData);
  SummaryBlock *sum = curseg->sum_blk;
  int i;

  std::lock_guard curseg_lock(curseg->curseg_mutex);

  {
    fs::SharedLock nat_lock(nat_tree_lock_);
    size_t dirty_nat_cnt = dirty_nat_list_.size_slow();
    if ((NatsInCursum(sum) + dirty_nat_cnt) <= kNatJournalEntries) {
      return false;
    }
  }

  for (i = 0; i < NatsInCursum(sum); ++i) {
    NatEntry *cache_entry = nullptr;
    RawNatEntry raw_entry = NatInJournal(sum, i);
    nid_t nid = LeToCpu(NidInJournal(sum, i));

    while (!cache_entry) {
      std::lock_guard nat_lock(nat_tree_lock_);
      cache_entry = LookupNatCache(nid);
      if (cache_entry) {
        SetNatCacheDirty(*cache_entry);
      } else {
        cache_entry = GrabNatEntry(nid);
        if (!cache_entry) {
          continue;
        }
        cache_entry->SetBlockAddress(LeToCpu(raw_entry.block_addr));
        cache_entry->SetIno(LeToCpu(raw_entry.ino));
        cache_entry->SetVersion(raw_entry.version);
        SetNatCacheDirty(*cache_entry);
      }
    }
  }
  UpdateNatsInCursum(sum, -i);
  return true;
}

// This function is called during the checkpointing process.
void NodeManager::FlushNatEntries() {
  CursegInfo *curseg = fs_->GetSegmentManager().CURSEG_I(CursegType::kCursegHotData);
  SummaryBlock *sum = curseg->sum_blk;
  fbl::RefPtr<Page> page;
  NatBlock *nat_blk = nullptr;
  nid_t start_nid = 0, end_nid = 0;
  bool flushed;

  flushed = FlushNatsInJournal();

#if 0  // porting needed
  //	if (!flushed)
#endif
  std::lock_guard curseg_lock(curseg->curseg_mutex);

  // 1) flush dirty nat caches
  {
    std::lock_guard nat_lock(nat_tree_lock_);
    for (auto iter = dirty_nat_list_.begin(); iter != dirty_nat_list_.end();) {
      nid_t nid;
      RawNatEntry raw_ne;
      int offset = -1;
      __UNUSED block_t old_blkaddr, new_blkaddr;

      // During each iteration, |iter| can be removed from |dirty_nat_list_|.
      // Therefore, make a copy of |iter| and move to the next element before futher operations.
      NatEntry *cache_entry = iter.CopyPointer();
      ++iter;

      nid = cache_entry->GetNid();

      if (cache_entry->GetBlockAddress() == kNewAddr)
        continue;

      if (!flushed) {
        // if there is room for nat enries in curseg->sumpage
        offset = LookupJournalInCursum(sum, JournalType::kNatJournal, nid, 1);
      }

      if (offset >= 0) {  // flush to journal
        raw_ne = NatInJournal(sum, offset);
        old_blkaddr = LeToCpu(raw_ne.block_addr);
      } else {  // flush to NAT block
        if (!page || (start_nid > nid || nid > end_nid)) {
          if (page) {
            page->SetDirty();
            Page::PutPage(std::move(page), true);
          }
          start_nid = StartNid(nid);
          end_nid = start_nid + kNatEntryPerBlock - 1;

          // get nat block with dirty flag, increased reference
          // count, mapped and lock
          GetNextNatPage(start_nid, &page);
          nat_blk = static_cast<NatBlock *>(page->GetAddress());
        }

        ZX_ASSERT(nat_blk);
        raw_ne = nat_blk->entries[nid - start_nid];
        old_blkaddr = LeToCpu(raw_ne.block_addr);
      }

      new_blkaddr = cache_entry->GetBlockAddress();

      raw_ne.ino = CpuToLe(cache_entry->GetIno());
      raw_ne.block_addr = CpuToLe(new_blkaddr);
      raw_ne.version = cache_entry->GetVersion();

      if (offset < 0) {
        nat_blk->entries[nid - start_nid] = raw_ne;
      } else {
        SetNatInJournal(sum, offset, raw_ne);
        SetNidInJournal(sum, offset, CpuToLe(nid));
      }

      if (cache_entry->GetBlockAddress() == kNullAddr) {
        DelFromNatCache(*cache_entry);
        // We can reuse this freed nid at this point
        AddFreeNid(nid);
      } else {
        ClearNatCacheDirty(*cache_entry);
        cache_entry->SetCheckpointed();
      }
    }
  }

  // Write out last modified NAT block
  if (page != nullptr) {
    page->SetDirty();
    Page::PutPage(std::move(page), true);
  }

  // 2) shrink nat caches if necessary
  TryToFreeNats(nat_entries_count_ - kNmWoutThreshold);
}

zx_status_t NodeManager::InitNodeManager() {
  const Superblock &sb_raw = GetSuperblockInfo().GetRawSuperblock();
  uint8_t *version_bitmap;
  uint32_t nat_segs, nat_blocks;

  nat_blkaddr_ = LeToCpu(sb_raw.nat_blkaddr);
  // segment_count_nat includes pair segment so divide to 2
  nat_segs = LeToCpu(sb_raw.segment_count_nat) >> 1;
  nat_blocks = nat_segs << LeToCpu(sb_raw.log_blocks_per_seg);
  max_nid_ = kNatEntryPerBlock * nat_blocks;
  free_nid_count_ = 0;
  nat_entries_count_ = 0;

  list_initialize(&free_nid_list_);

  nat_bitmap_size_ = GetSuperblockInfo().BitmapSize(MetaBitmap::kNatBitmap);
  init_scan_nid_ = LeToCpu(GetSuperblockInfo().GetCheckpoint().next_free_nid);
  next_scan_nid_ = LeToCpu(GetSuperblockInfo().GetCheckpoint().next_free_nid);

  nat_bitmap_ = std::make_unique<uint8_t[]>(nat_bitmap_size_);
  memset(nat_bitmap_.get(), 0, nat_bitmap_size_);
  nat_prev_bitmap_ = std::make_unique<uint8_t[]>(nat_bitmap_size_);
  memset(nat_prev_bitmap_.get(), 0, nat_bitmap_size_);

  version_bitmap = static_cast<uint8_t *>(GetSuperblockInfo().BitmapPtr(MetaBitmap::kNatBitmap));
  if (!version_bitmap)
    return ZX_ERR_INVALID_ARGS;

  // copy version bitmap
  memcpy(nat_bitmap_.get(), version_bitmap, nat_bitmap_size_);
  memcpy(nat_prev_bitmap_.get(), nat_bitmap_.get(), nat_bitmap_size_);
  return ZX_OK;
}

zx_status_t NodeManager::BuildNodeManager() {
  if (zx_status_t err = InitNodeManager(); err != ZX_OK) {
    return err;
  }

  BuildFreeNids();
  return ZX_OK;
}

void NodeManager::DestroyNodeManager() {
  FreeNid *i, *next_i;
  NatEntry *natvec[kNatvecSize];
  uint32_t found;

  {
    // destroy free nid list
    std::lock_guard free_nid_lock(free_nid_list_lock_);
    list_for_every_entry_safe (&free_nid_list_, i, next_i, FreeNid, list) {
      ZX_ASSERT(i->state != static_cast<int>(NidState::kNidAlloc));
      DelFromFreeNidList(i);
      --free_nid_count_;
    }
  }
  ZX_ASSERT(!free_nid_count_);

  {
    // destroy nat cache
    std::lock_guard nat_lock(nat_tree_lock_);
    while ((found = GangLookupNatCache(kNatvecSize, natvec))) {
      for (uint32_t idx = 0; idx < found; ++idx) {
        NatEntry *e = natvec[idx];
        DelFromNatCache(*e);
      }
    }
    ZX_ASSERT(!nat_entries_count_);
    ZX_ASSERT(clean_nat_list_.is_empty());
    ZX_ASSERT(dirty_nat_list_.is_empty());
    ZX_ASSERT(nat_cache_.is_empty());
  }

  nat_bitmap_.reset();
  nat_prev_bitmap_.reset();
}

block_t NodeManager::StartBidxOfNode(Page &node_page) {
  uint32_t node_ofs = OfsOfNode(node_page);
  block_t start_bidx;
  unsigned int bidx, indirect_blks;
  int dec;

  indirect_blks = 2 * kNidsPerBlock + 4;

  start_bidx = 1;
  if (node_ofs == 0) {
    start_bidx = 0;
  } else if (node_ofs <= 2) {
    bidx = node_ofs - 1;
  } else if (node_ofs <= indirect_blks) {
    dec = (node_ofs - 4) / (kNidsPerBlock + 1);
    bidx = node_ofs - 2 - dec;
  } else {
    dec = (node_ofs - indirect_blks - 3) / (kNidsPerBlock + 1);
    bidx = node_ofs - 5 - dec;
  }

  if (start_bidx)
    start_bidx = bidx * kAddrsPerBlock + kAddrsPerInode;
  return start_bidx;
}

SuperblockInfo &NodeManager::GetSuperblockInfo() { return *superblock_info_; }

}  // namespace f2fs
