// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/f2fs/f2fs.h"

namespace f2fs {

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

  fbl::AutoLock stat_lock(&sbi_->stat_lock);

  valid_block_count = sbi_->total_valid_block_count + static_cast<block_t>(count);
  sbi_->alloc_valid_block_count += static_cast<block_t>(count);
  ValidNodeCount = sbi_->total_valid_node_count + count;

  if (valid_block_count > sbi_->user_block_count) {
    return false;
  }

  if (ValidNodeCount > sbi_->total_node_count) {
    return false;
  }

  if (vnode)
    vnode->IncBlocks(count);
  sbi_->total_valid_node_count = ValidNodeCount;
  sbi_->total_valid_block_count = valid_block_count;

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
  seg_off = block_off >> sbi_->log_blocks_per_seg;

  block_addr = static_cast<pgoff_t>(nat_blkaddr_ + (seg_off << sbi_->log_blocks_per_seg << 1) +
                                    (block_off & ((1 << sbi_->log_blocks_per_seg) - 1)));

  if (TestValidBitmap(block_off, nat_bitmap_.get()))
    block_addr += sbi_->blocks_per_seg;

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
  if ((block_addr >> sbi_->log_blocks_per_seg) % 2)
    block_addr -= sbi_->blocks_per_seg;
  else
    block_addr += sbi_->blocks_per_seg;

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
  void *kaddr = PageAddress(page);
  Node *rn = static_cast<Node *>(kaddr);
  if (reset)
    memset(rn, 0, sizeof(*rn));
  rn->footer.nid = CpuToLe(nid);
  rn->footer.ino = CpuToLe(ino);
  rn->footer.flag = CpuToLe(ofs << static_cast<int>(BitShift::kOffsetBitShift));
}

void NodeManager::CopyNodeFooter(Page &dst, Page &src) {
  void *src_addr = PageAddress(src);
  void *dst_addr = PageAddress(dst);
  Node *src_rn = static_cast<Node *>(src_addr);
  Node *dst_rn = static_cast<Node *>(dst_addr);
  memcpy(&dst_rn->footer, &src_rn->footer, sizeof(NodeFooter));
}

void NodeManager::FillNodeFooterBlkaddr(Page *page, block_t blkaddr) {
  Checkpoint *ckpt = GetCheckpoint(sbi_);
  void *kaddr = PageAddress(page);
  Node *rn = static_cast<Node *>(kaddr);
  rn->footer.cp_ver = ckpt->checkpoint_ver;
  rn->footer.next_blkaddr = blkaddr;
}

nid_t NodeManager::InoOfNode(Page &node_page) {
  void *kaddr = PageAddress(node_page);
  Node *rn = static_cast<Node *>(kaddr);
  return LeToCpu(rn->footer.ino);
}

nid_t NodeManager::NidOfNode(Page &node_page) {
  void *kaddr = PageAddress(node_page);
  Node *rn = static_cast<Node *>(kaddr);
  return LeToCpu(rn->footer.nid);
}

uint32_t NodeManager::OfsOfNode(Page &node_page) {
  void *kaddr = PageAddress(node_page);
  Node *rn = static_cast<Node *>(kaddr);
  uint32_t flag = LeToCpu(rn->footer.flag);
  return flag >> static_cast<int>(BitShift::kOffsetBitShift);
}

uint64_t NodeManager::CpverOfNode(Page &node_page) {
  void *kaddr = PageAddress(node_page);
  Node *rn = static_cast<Node *>(kaddr);
  return LeToCpu(rn->footer.cp_ver);
}

block_t NodeManager::NextBlkaddrOfNode(Page &node_page) {
  void *kaddr = PageAddress(node_page);
  Node *rn = static_cast<Node *>(kaddr);
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

void NodeManager::SetNid(Page &p, int off, nid_t nid, bool i) {
  Node *rn = static_cast<Node *>(PageAddress(&p));

  WaitOnPageWriteback(&p);

  if (i) {
    rn->i.i_nid[off - kNodeDir1Block] = CpuToLe(nid);
  } else {
    rn->in.nid[off] = CpuToLe(nid);
  }

#if 0  // porting needed
  // set_page_dirty(p);
#endif
  FlushDirtyNodePage(fs_, &p);
}

nid_t NodeManager::GetNid(Page &p, int off, bool i) {
  Node *rn = static_cast<Node *>(PageAddress(&p));
  if (i)
    return LeToCpu(rn->i.i_nid[off - kNodeDir1Block]);
  return LeToCpu(rn->in.nid[off]);
}

// Coldness identification:
//  - Mark cold files in InodeInfo
//  - Mark cold node blocks in their node footer
//  - Mark cold data pages in page cache
bool NodeManager::IsColdFile(VnodeF2fs &vnode) { return (vnode.IsAdviseSet(FAdvise::kCold) != 0); }

#if 0  // porting needed
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
  void *kaddr = PageAddress(page);
  Node *rn = static_cast<Node *>(kaddr);
  uint32_t flag = LeToCpu(rn->footer.flag);
  return flag & (0x1 << static_cast<int>(BitShift::kColdBitShift));
}

uint8_t NodeManager::IsFsyncDnode(Page &page) {
  void *kaddr = PageAddress(page);
  Node *rn = static_cast<Node *>(kaddr);
  uint32_t flag = LeToCpu(rn->footer.flag);
  return flag & (0x1 << static_cast<int>(BitShift::kFsyncBitShift));
}

uint8_t NodeManager::IsDentDnode(Page &page) {
  void *kaddr = PageAddress(page);
  Node *rn = static_cast<Node *>(kaddr);
  uint32_t flag = LeToCpu(rn->footer.flag);
  return flag & (0x1 << static_cast<int>(BitShift::kDentBitShift));
}

void NodeManager::SetColdNode(VnodeF2fs &vnode, Page &page) {
  Node *rn = static_cast<Node *>(PageAddress(page));
  uint32_t flag = LeToCpu(rn->footer.flag);

  if (vnode.IsDir())
    flag &= ~(0x1 << static_cast<int>(BitShift::kColdBitShift));
  else
    flag |= (0x1 << static_cast<int>(BitShift::kColdBitShift));
  rn->footer.flag = CpuToLe(flag);
}

void NodeManager::SetFsyncMark(Page &page, int mark) {
  void *kaddr = PageAddress(page);
  Node *rn = static_cast<Node *>(kaddr);
  uint32_t flag = LeToCpu(rn->footer.flag);
  if (mark)
    flag |= (0x1 << static_cast<int>(BitShift::kFsyncBitShift));
  else
    flag &= ~(0x1 << static_cast<int>(BitShift::kFsyncBitShift));
  rn->footer.flag = CpuToLe(flag);
}

void NodeManager::SetDentryMark(Page &page, int mark) {
  void *kaddr = PageAddress(page);
  Node *rn = static_cast<Node *>(kaddr);
  uint32_t flag = LeToCpu(rn->footer.flag);
  if (mark)
    flag |= (0x1 << static_cast<int>(BitShift::kDentBitShift));
  else
    flag &= ~(0x1 << static_cast<int>(BitShift::kDentBitShift));
  rn->footer.flag = CpuToLe(flag);
}

void NodeManager::DecValidNodeCount(VnodeF2fs *vnode, uint32_t count) {
  fbl::AutoLock stat_lock(&sbi_->stat_lock);

  ZX_ASSERT(!(sbi_->total_valid_block_count < count));
  ZX_ASSERT(!(sbi_->total_valid_node_count < count));

  vnode->DecBlocks(count);
  sbi_->total_valid_node_count -= count;
  sbi_->total_valid_block_count -= count;
}

NodeManager::NodeManager(F2fs *fs) : fs_(fs) {
  if (fs) {
    sbi_ = &fs->GetSbInfo();
  }
}

NodeManager::NodeManager(SbInfo *sbi) : sbi_(sbi) {}

void NodeManager::ClearNodePageDirty(Page *page) {
#if 0  // porting needed
  // address_space *mapping = page->mapping;
  // uint32_t long flags;
#endif

  if (PageDirty(page)) {
#if 0  // porting needed
    // TODO: IMPL
    // SpinLock_irqsave(&mapping->tree_lock, flags);
    // radix_tree_tag_clear(&mapping->page_tree,
    //		page_index(page),
    //		PAGECACHE_TAG_DIRTY);
    // SpinUnlock_irqrestore(&mapping->tree_lock, flags);
#endif
    ClearPageDirtyForIo(page);
    DecPageCount(sbi_, CountType::kDirtyNodes);
  }
  ClearPageUptodate(page);
}

Page *NodeManager::GetCurrentNatPage(nid_t nid) {
  pgoff_t index = CurrentNatAddr(nid);
  return fs_->GetMetaPage(index);
}

Page *NodeManager::GetNextNatPage(nid_t nid) {
  Page *src_page;
  Page *dst_page;
  pgoff_t src_off;
  pgoff_t dst_off;
  void *src_addr;
  void *dst_addr;

  src_off = CurrentNatAddr(nid);
  dst_off = NextNatAddr(src_off);

  // get current nat block page with lock
  src_page = fs_->GetMetaPage(src_off);

  // Dirty src_page means that it is already the new target NAT page
#if 0  // porting needed
  // if (PageDirty(src_page))
#endif
  if (IsUpdatedNatPage(nid))
    return src_page;

  dst_page = fs_->GrabMetaPage(dst_off);

  src_addr = PageAddress(src_page);
  dst_addr = PageAddress(dst_page);
  memcpy(dst_addr, src_addr, kPageCacheSize);
#if 0  // porting needed
  //   set_page_dirty(dst_page);
#endif
  F2fsPutPage(src_page, 1);

  SetToNextNat(nid);

  return dst_page;
}

/**
 * Readahead NAT pages
 */
void NodeManager::RaNatPages(nid_t nid) {
  Page *page;
  pgoff_t index;
  int i;

  for (i = 0; i < kFreeNidPages; i++, nid += kNatEntryPerBlock) {
    if (nid >= max_nid_)
      nid = 0;
    index = CurrentNatAddr(nid);

    page = GrabCachePage(nullptr, MetaIno(sbi_), index);
    if (!page)
      continue;
    if (VnodeF2fs::Readpage(fs_, page, static_cast<block_t>(index), 0 /*READ*/)) {
      F2fsPutPage(page, 1);
      continue;
    }
#if 0  // porting needed
    // page_cache_release(page);
#endif
    F2fsPutPage(page, 1);
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
    nr_shrink--;
  }
  return nr_shrink;
}

// This function returns always success
void NodeManager::GetNodeInfo(nid_t nid, NodeInfo &out) {
  CursegInfo *curseg = SegMgr::CURSEG_I(sbi_, CursegType::kCursegHotData);
  SummaryBlock *sum = curseg->sum_blk;
  nid_t start_nid = StartNid(nid);
  NatBlock *nat_blk;
  Page *page = NULL;
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
    fbl::AutoLock curseg_lock(&curseg->curseg_mutex);
    i = SegMgr::LookupJournalInCursum(sum, JournalType::kNatJournal, nid, 0);
    if (i >= 0) {
      ne = NatInJournal(sum, i);
      NodeInfoFromRawNat(out, ne);
    }
  }
  if (i < 0) {
    // Fill NodeInfo from nat page
    page = GetCurrentNatPage(start_nid);
    nat_blk = static_cast<NatBlock *>(PageAddress(page));
    ne = nat_blk->entries[nid - start_nid];

    NodeInfoFromRawNat(out, ne);
    F2fsPutPage(page, 1);
  }
  CacheNatEntry(nid, ne);
}

// The maximum depth is four.
// Offset[0] will have raw inode offset.
zx::status<int> NodeManager::GetNodePath(long block, int (&offset)[4], uint32_t (&noffset)[4]) {
  const long direct_index = kAddrsPerInode;
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
      zx::error(ZX_ERR_NOT_FOUND);
    }
  } while (false);
  return zx::ok(level);
}

// Caller should call f2fs_put_dnode(dn).
zx_status_t NodeManager::GetDnodeOfData(DnodeOfData &dn, pgoff_t index, int ro) {
  Page *npage[4];
  Page *parent;
  int offset[4];
  uint32_t noffset[4];
  nid_t nids[4];
  int level, i;
  zx_status_t err = 0;
  auto node_path = GetNodePath(index, offset, noffset);
  auto release_pages = [&]() {
    F2fsPutPage(parent, 1);
    if (i > 1) {
      F2fsPutPage(npage[0], 0);
    }
    dn.inode_page = nullptr;
    dn.node_page = nullptr;
  };
  auto release_out = [&dn]() {
    dn.inode_page = nullptr;
    dn.node_page = nullptr;
  };
  if (node_path.is_error())
    return node_path.error_value();

  level = *node_path;

  nids[0] = dn.vnode->Ino();
  npage[0] = nullptr;
  err = GetNodePage(nids[0], &npage[0]);
  if (err)
    return err;

  parent = npage[0];
  if (level != 0)
    nids[1] = GetNid(*parent, offset[0], true);
  dn.inode_page = npage[0];
  dn.inode_page_locked = true;

  /* get indirect or direct nodes */
  for (i = 1; i <= level; i++) {
    bool done = false;

    if (!nids[i] && !ro) {
      /* alloc new node */
      if (!AllocNid(&(nids[i]))) {
        err = ZX_ERR_NO_SPACE;
        release_pages();
        return err;
      }

      dn.nid = nids[i];
      npage[i] = nullptr;
      err = NewNodePage(dn, noffset[i], &npage[i]);
      if (err) {
        AllocNidFailed(nids[i]);
        release_pages();
        return err;
      }

      SetNid(*parent, offset[i - 1], nids[i], i == 1);
      AllocNidDone(nids[i]);
      done = true;
    } else if (ro && i == level && level > 1) {
#if 0  // porting needed
      // err = GetNodePageRa(parent, offset[i - 1], &npage[i]);
      // if (err) {
      // 	release_pages();
      // 	return err;
      // }
      // done = true;
#endif
    }
    if (i == 1) {
      dn.inode_page_locked = false;
#if 0  // porting needed
      // unlock_page(parent);
#endif
    } else {
      F2fsPutPage(parent, 1);
    }

    if (!done) {
      npage[i] = nullptr;
      err = GetNodePage(nids[i], &npage[i]);
      if (err) {
        F2fsPutPage(npage[0], 0);
        release_out();
        return err;
      }
    }
    if (i < level) {
      parent = npage[i];
      nids[i + 1] = GetNid(*parent, offset[i], false);
    }
  }
  dn.nid = nids[level];
  dn.ofs_in_node = offset[level];
  dn.node_page = npage[level];
  dn.data_blkaddr = DatablockAddr(dn.node_page, dn.ofs_in_node);

#ifdef F2FS_BU_DEBUG
  FX_LOGS(DEBUG) << "NodeManager::GetDnodeOfData"
                 << ", dn.nid=" << dn.nid << ", dn.node_page=" << dn.node_page
                 << ", dn.ofs_in_node=" << dn.ofs_in_node
                 << ", dn.data_blkaddr=" << dn.data_blkaddr;
#endif
  return ZX_OK;
}

void NodeManager::TruncateNode(DnodeOfData &dn) {
  NodeInfo ni;

  GetNodeInfo(dn.nid, ni);
  ZX_ASSERT(ni.blk_addr != kNullAddr);

  if (ni.blk_addr != kNullAddr)
    fs_->Segmgr().InvalidateBlocks(ni.blk_addr);

  // Deallocate node address
  DecValidNodeCount(dn.vnode, 1);
  SetNodeAddr(ni, kNullAddr);

  if (dn.nid == dn.vnode->Ino()) {
    fs_->RemoveOrphanInode(dn.nid);
    fs_->DecValidInodeCount();
  } else {
    SyncInodePage(dn);
  }

  ClearNodePageDirty(dn.node_page);
  SetSbDirt(sbi_);

  F2fsPutPage(dn.node_page, 1);
  dn.node_page = nullptr;
}

zx_status_t NodeManager::TruncateDnode(DnodeOfData &dn) {
  Page *page = nullptr;
  zx_status_t err = 0;

  if (dn.nid == 0)
    return 1;

  // get direct node
  err = fs_->GetNodeManager().GetNodePage(dn.nid, &page);
  if (err && err == ZX_ERR_NOT_FOUND)
    return 1;
  else if (err)
    return err;

  dn.node_page = page;
  dn.ofs_in_node = 0;
  dn.vnode->TruncateDataBlocks(&dn);
  TruncateNode(dn);
  return 1;
}

zx_status_t NodeManager::TruncateNodes(DnodeOfData &dn, uint32_t nofs, int ofs, int depth) {
  DnodeOfData rdn = dn;
  Page *page = nullptr;
  Node *rn;
  nid_t child_nid;
  uint32_t child_nofs;
  int freed = 0;
  int i, ret;
  zx_status_t err = 0;

  if (dn.nid == 0)
    return kNidsPerBlock + 1;

  err = fs_->GetNodeManager().GetNodePage(dn.nid, &page);
  if (err)
    return err;

  rn = (Node *)PageAddress(page);
  if (depth < 3) {
    for (i = ofs; i < kNidsPerBlock; i++, freed++) {
      child_nid = LeToCpu(rn->in.nid[i]);
      if (child_nid == 0)
        continue;
      rdn.nid = child_nid;
      ret = TruncateDnode(rdn);
      if (ret < 0) {
        F2fsPutPage(page, 1);
        return ret;
      }
      SetNid(*page, i, 0, false);
    }
  } else {
    child_nofs = nofs + ofs * (kNidsPerBlock + 1) + 1;
    for (i = ofs; i < kNidsPerBlock; i++) {
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
        F2fsPutPage(page, 1);
        return ret;
      }
    }
    freed = child_nofs;
  }

  if (!ofs) {
    // remove current indirect node
    dn.node_page = page;
    TruncateNode(dn);
    freed++;
  } else {
    F2fsPutPage(page, 1);
  }
  return freed;
}

zx_status_t NodeManager::TruncatePartialNodes(DnodeOfData &dn, Inode &ri, int (&offset)[4],
                                              int depth) {
  Page *pages[2];
  nid_t nid[3];
  nid_t child_nid;
  zx_status_t err = 0;
  int i;
  int idx = depth - 2;
  auto free_pages = [&]() {
    for (int i = depth - 3; i >= 0; i--) {
      F2fsPutPage(pages[i], 1);
    }
  };

  nid[0] = LeToCpu(ri.i_nid[offset[0] - kNodeDir1Block]);
  if (!nid[0])
    return ZX_OK;

  /* get indirect nodes in the path */
  for (i = 0; i < depth - 1; i++) {
    /* refernece count'll be increased */
    pages[i] = nullptr;
    err = fs_->GetNodeManager().GetNodePage(nid[i], &pages[i]);
    if (err) {
      depth = i + 1;
      free_pages();
      return err;
    }
    nid[i + 1] = GetNid(*pages[i], offset[i + 1], false);
  }

  // free direct nodes linked to a partial indirect node
  for (i = offset[depth - 1]; i < kNidsPerBlock; i++) {
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

  if (offset[depth - 1] == 0) {
    dn.node_page = pages[idx];
    dn.nid = nid[idx];
    TruncateNode(dn);
  } else {
    F2fsPutPage(pages[idx], 1);
  }
  offset[idx]++;
  offset[depth - 1] = 0;
  return err;
}

// All the block addresses of data and nodes should be nullified.
zx_status_t NodeManager::TruncateInodeBlocks(VnodeF2fs &vnode, pgoff_t from) {
  int cont = 1;
  int level, offset[4];
  uint32_t nofs, noffset[4];
  Node *rn;
  DnodeOfData dn;
  Page *page = nullptr;
  zx_status_t err = 0;

  auto node_path = GetNodePath(from, offset, noffset);
  if (node_path.is_error())
    return node_path.error_value();

  level = *node_path;

  err = GetNodePage(vnode.Ino(), &page);
  if (err)
    return err;

  SetNewDnode(dn, &vnode, page, nullptr, 0);
#if 0  // porting needed
  // unlock_page(page);
#endif

  rn = static_cast<Node *>(PageAddress(page));
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
        F2fsPutPage(page, 0);
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
        F2fsPutPage(page, 0);
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
      F2fsPutPage(page, 0);
      return err;
    }
    if (offset[1] == 0 && rn->i.i_nid[offset[0] - kNodeDir1Block]) {
#if 0  // porting needed
      // lock_page(page);
#endif
      WaitOnPageWriteback(page);
      rn->i.i_nid[offset[0] - kNodeDir1Block] = 0;
#if 0  // porting needed
      // set_page_dirty(page);
#endif
      FlushDirtyNodePage(fs_, page);
#if 0  // porting needed
      // unlock_page(page);
#endif
    }
    offset[1] = 0;
    offset[0]++;
    nofs += err;
  }
  F2fsPutPage(page, 0);
  return err > 0 ? 0 : err;
}

zx_status_t NodeManager::RemoveInodePage(VnodeF2fs *vnode) {
  Page *page = nullptr;
  nid_t ino = vnode->Ino();
  DnodeOfData dn;
  zx_status_t err = 0;

  err = GetNodePage(ino, &page);
  if (err) {
    return err;
  }

  if (nid_t nid = vnode->GetXattrNid(); nid > 0) {
    Page *npage = nullptr;
    err = GetNodePage(nid, &npage);

    if (err) {
      return err;
    }

    vnode->ClearXattrNid();

    SetNewDnode(dn, vnode, page, npage, nid);
    dn.inode_page_locked = true;
    TruncateNode(dn);
  }
  if (vnode->GetBlocks() == 1) {
    // internally call f2fs_put_page()
    SetNewDnode(dn, vnode, page, page, ino);
    TruncateNode(dn);
  } else if (vnode->GetBlocks() == 0) {
    NodeInfo ni;
    GetNodeInfo(vnode->Ino(), ni);

    ZX_ASSERT(ni.blk_addr == kNullAddr);
    F2fsPutPage(page, 1);
  } else {
    ZX_ASSERT(0);
  }
  return ZX_OK;
}

zx_status_t NodeManager::NewInodePage(Dir *parent, VnodeF2fs *child) {
  Page *page = nullptr;
  DnodeOfData dn;
  zx_status_t err = 0;

  // allocate inode page for new inode
  SetNewDnode(dn, child, nullptr, nullptr, child->Ino());
  err = NewNodePage(dn, 0, &page);
  parent->InitDentInode(child, page);

  if (err)
    return err;
  F2fsPutPage(page, 1);
  return ZX_OK;
}

zx_status_t NodeManager::NewNodePage(DnodeOfData &dn, uint32_t ofs, Page **out) {
  NodeInfo old_ni, new_ni;
  Page *page = nullptr;
  int err;

  if (dn.vnode->TestFlag(InodeInfoFlag::kNoAlloc))
    return ZX_ERR_ACCESS_DENIED;

  page = GrabCachePage(nullptr, NodeIno(sbi_), dn.nid);
  if (!page)
    return ZX_ERR_NO_MEMORY;

  GetNodeInfo(dn.nid, old_ni);

  SetPageUptodate(page);
  FillNodeFooter(*page, dn.nid, dn.vnode->Ino(), ofs, true);

  // Reinitialize old_ni with new node page
  ZX_ASSERT(old_ni.blk_addr == kNullAddr);
  new_ni = old_ni;
  new_ni.ino = dn.vnode->Ino();

  if (!IncValidNodeCount(dn.vnode, 1)) {
    err = ZX_ERR_NO_SPACE;
    F2fsPutPage(page, 1);
    return err;
  }
  SetNodeAddr(new_ni, kNewAddr);

  dn.node_page = page;
  SyncInodePage(dn);

#if 0  // porting needed
  //   set_page_dirty(page);
#endif
  SetColdNode(*dn.vnode, *page);
  FlushDirtyNodePage(fs_, page);
  if (ofs == 0)
    fs_->IncValidInodeCount();

  *out = page;
  return ZX_OK;
}

zx_status_t NodeManager::ReadNodePage(Page &page, nid_t nid, int type) {
  NodeInfo ni;

  GetNodeInfo(nid, ni);

  if (ni.blk_addr == kNullAddr)
    return ZX_ERR_NOT_FOUND;

  if (ni.blk_addr == kNewAddr) {
#ifdef F2FS_BU_DEBUG
    FX_LOGS(DEBUG) << "NodeManager::ReadNodePage, Read New address...";
#endif
    return ZX_OK;
  }

  return VnodeF2fs::Readpage(fs_, &page, ni.blk_addr, type);
}

#if 0  // porting needed
// TODO: Readahead a node page
void NodeManager::RaNodePage(nid_t nid) {
  // TODO: IMPL Read ahead
}
#endif

zx_status_t NodeManager::GetNodePage(nid_t nid, Page **out) {
  int err;
  Page *page = nullptr;
#if 0  // porting needed
  // address_space *mapping = sbi_->node_inode->i_mapping;
#endif

  page = GrabCachePage(nullptr, NodeIno(sbi_), nid);
  if (!page)
    return ZX_ERR_NO_MEMORY;

  err = ReadNodePage(*page, nid, kReadSync);
  if (err) {
    F2fsPutPage(page, 1);
    return err;
  }

  ZX_ASSERT(nid == NidOfNode(*page));
#if 0  // porting needed
  // mark_page_accessed(page);
#endif
  *out = page;
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
  if (dn.vnode->GetNlink())
    dn.vnode->MarkInodeDirty();
#if 0  // porting needed
  if (IsInode(dn->node_page) || dn->inode_page == dn->node_page) {
    dn->vnode->UpdateInode(dn->node_page);
  } else if (dn->inode_page) {
    if (!dn->inode_page_locked)
      // lock_page(dn->inode_page);
      dn->vnode->UpdateInode(dn->inode_page);
    // if (!dn->inode_page_locked)
    //  unlock_page(dn->inode_page);
  } else {
    dn->vnode->WriteInode(nullptr);
  }
#endif
}

int NodeManager::SyncNodePages(nid_t ino, WritebackControl *wbc) {
  zx_status_t status = fs_->GetVCache().ForDirtyVnodesIf(
      [this](fbl::RefPtr<VnodeF2fs> &vnode) {
        if (!vnode->ShouldFlush()) {
          return ZX_ERR_NEXT;
        }
        ZX_ASSERT(vnode->WriteInode(nullptr) == ZX_OK);
        ZX_ASSERT(fs_->GetVCache().RemoveDirty(vnode.get()) == ZX_OK);
        ZX_ASSERT(vnode->ClearDirty() == true);
        return ZX_OK;
      },
      [](fbl::RefPtr<VnodeF2fs> &vnode) {
        if (!vnode->ShouldFlush()) {
          return ZX_ERR_NEXT;
        }
        return ZX_OK;
      });
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to flush dirty vnodes ";
  }

#if 0  // porting needed
  // SbInfo &sbi = fs_->GetSbInfo();
  // //address_space *mapping = sbi.node_inode->i_mapping;
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
  // 		int i, nr_pages;
  //  TODO: IMPL
  // nr_pages = pagevec_lookup_tag(&pvec, mapping, &index,
  // 		PAGECACHE_TAG_DIRTY,
  // 		min(end - index, (pgoff_t)PAGEVEC_SIZE-1) + 1);
  // if (nr_pages == 0)
  // 	break;

  // 		for (i = 0; i < nr_pages; i++) {
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
  // 				int mark = !IsCheckpointedNode(sbi, ino);
  // 				SetFsyncMark(page, 1);
  // 				if (IsInode(page))
  // 					SetDentryMark(page, mark);
  // 				nwritten++;
  // 			} else {
  // 				SetFyncMark(page, 0);
  // 				SetDentryMark(page, 0);
  // 			}
  // 			mapping->a_ops->writepage(page, wbc);
  // 			wrote++;

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
  // 		step++;
  // 		goto next_step;
  // 	}

  // 	if (wrote)
  // 		f2fs_submit_bio(sbi, NODE, wbc->sync_mode == WB_SYNC_ALL);

  //	return nwritten;
#endif
  return 0;
}

zx_status_t NodeManager::F2fsWriteNodePage(Page &page, WritebackControl *wbc) {
  nid_t nid;
  __UNUSED uint32_t nofs;
  block_t new_addr;
  NodeInfo ni;

#if 0  // porting needed
  // 	if (wbc->for_reclaim) {
  // 		DecPageCount(&sbi, CountType::kDirtyNodes);
  // 		wbc->pages_skipped++;
  //		// set_page_dirty(page);
  //		FlushDirtyNodePage(fs_, page);
  // 		return kAopWritepageActivate;
  // 	}
#endif
  WaitOnPageWriteback(&page);

  // get old block addr of this node page
  nid = NidOfNode(page);
  nofs = OfsOfNode(page);
  ZX_ASSERT(page.index == nid);

  GetNodeInfo(nid, ni);

  // This page is already truncated
  if (ni.blk_addr == kNullAddr) {
    return ZX_OK;
  }

  {
    fs::SharedLock rlock(sbi_->fs_lock[static_cast<int>(LockType::kNodeOp)]);
    SetPageWriteback(&page);

    // insert node offset
    fs_->Segmgr().WriteNodePage(&page, nid, ni.blk_addr, &new_addr);
    SetNodeAddr(ni, new_addr);
    DecPageCount(sbi_, CountType::kDirtyNodes);
  }

  // TODO: IMPL
  // unlock_page(page);
  return ZX_OK;
}

#if 0  // porting needed
int NodeManager::F2fsWriteNodePages(struct address_space *mapping, WritebackControl *wbc) {
  // struct SbInfo *sbi = F2FS_SB(mapping->host->i_sb);
  // struct block_device *bdev = sbi->sb->s_bdev;
  // long nr_to_write = wbc->nr_to_write;

  // if (wbc->for_kupdate)
  // 	return 0;

  // if (GetPages(sbi, CountType::kDirtyNodes) == 0)
  // 	return 0;

  // if (try_to_free_nats(sbi, kNatEntryPerBlock)) {
  // 	write_checkpoint(sbi, false, false);
  // 	return 0;
  // }

  // /* if mounting is failed, skip writing node pages */
  // wbc->nr_to_write = bio_get_nr_vecs(bdev);
  // sync_node_pages(sbi, 0, wbc);
  // wbc->nr_to_write = nr_to_write -
  // 	(bio_get_nr_vecs(bdev) - wbc->nr_to_write);
  // return 0;
  return 0;
}
#endif

#if 0  // porting needed
int NodeManager::F2fsSetNodePageDirty(Page *page) {
  SbInfo &sbi = fs_->GetSbInfo();

  SetPageUptodate(page);
  if (!PageDirty(page)) {
    // __set_page_dirty_nobuffers(page);
    FlushDirtyNodePage(fs_, page);
    IncPageCount(&sbi, CountType::kDirtyNodes);
    // SetPagePrivate(page);
    return 1;
  }
  return 0;
}
#endif

#if 0  // porting needed
void NodeManager::F2fsInvalidateNodePage(Page *page, uint64_t offset) {
  SbInfo &sbi = fs_->GetSbInfo();

  if (PageDirty(page))
    DecPageCount(&sbi, CountType::kDirtyNodes);
  ClearPagePrivate(page);
}
#endif

#if 0  // porting needed
int F2fsReleaseNodePage(Page *page, gfp_t wait) {
  ClearPagePrivate(page);
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
#if 0  // porting needed
  // kmem_cache_free(free_nid_slab, i);
#endif
  delete i;
}

int NodeManager::AddFreeNid(nid_t nid) {
  FreeNid *i;

  if (free_nid_count_ > 2 * kMaxFreeNids)
    return 0;
  do {
#if 0  // porting needed (kmem_cache_alloc)
  // i = kmem_cache_alloc(free_nid_slab, GFP_NOFS);
#endif
    i = new FreeNid;
  } while (!i);
#if 0  // porting needed
    // cond_resched();
#endif
  i->nid = nid;
  i->state = static_cast<int>(NidState::kNidNew);

  std::lock_guard free_nid_lock(free_nid_list_lock_);
  if (LookupFreeNidList(nid)) {
#if 0  // porting needed
    // kmem_cache_free(free_nid_slab, i);
#endif
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
  NatBlock *nat_blk = static_cast<NatBlock *>(PageAddress(&nat_page));
  block_t blk_addr;
  int fcnt = 0;
  uint32_t i;

  // 0 nid should not be used
  if (start_nid == 0)
    ++start_nid;

  i = start_nid % kNatEntryPerBlock;

  for (; i < kNatEntryPerBlock; i++, start_nid++) {
    blk_addr = LeToCpu(nat_blk->entries[i].block_addr);
    ZX_ASSERT(blk_addr != kNewAddr);
    if (blk_addr == kNullAddr)
      fcnt += AddFreeNid(start_nid);
  }
  return fcnt;
}

void NodeManager::BuildFreeNids() {
  FreeNid *fnid, *next_fnid;
  CursegInfo *curseg = SegMgr::CURSEG_I(sbi_, CursegType::kCursegHotData);
  SummaryBlock *sum = curseg->sum_blk;
  nid_t nid = 0;
  bool is_cycled = false;
  uint64_t fcnt = 0;
  int i;

  nid = next_scan_nid_;
  init_scan_nid_ = nid;

  RaNatPages(nid);

  while (true) {
    Page *page = GetCurrentNatPage(nid);

    fcnt += ScanNatPage(*page, nid);
    F2fsPutPage(page, 1);

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
    fbl::AutoLock curseg_lock(&curseg->curseg_mutex);
    for (i = 0; i < NatsInCursum(sum); i++) {
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
bool NodeManager::AllocNid(nid_t *out) {
  FreeNid *i = nullptr;
  list_node_t *this_list;
  do {
    {
      fbl::AutoLock lock(&build_lock_);
      if (!free_nid_count_) {
        // scan NAT in order to build free nid list
        BuildFreeNids();
        if (!free_nid_count_) {
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
  *out = i->nid;
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

void NodeManager::RecoverNodePage(Page &page, Summary &sum, NodeInfo &ni, block_t new_blkaddr) {
  fs_->Segmgr().RewriteNodePage(&page, &sum, ni.blk_addr, new_blkaddr);
  SetNodeAddr(ni, new_blkaddr);
  ClearNodePageDirty(&page);
}

zx_status_t NodeManager::RecoverInodePage(Page &page) {
  //[[maybe_unused]] address_space *mapping = sbi.node_inode->i_mapping;
  Node *src, *dst;
  nid_t ino = InoOfNode(page);
  NodeInfo old_ni, new_ni;
  Page *ipage = nullptr;

  ipage = GrabCachePage(nullptr, NodeIno(sbi_), ino);
  if (!ipage)
    return ZX_ERR_NO_MEMORY;

  // Should not use this inode  from free nid list
  RemoveFreeNid(ino);

  GetNodeInfo(ino, old_ni);

#if 0  // porting needed
  // SetPageUptodate(ipage);
#endif
  FillNodeFooter(*ipage, ino, ino, 0, true);

  src = static_cast<Node *>(PageAddress(page));
  dst = static_cast<Node *>(PageAddress(ipage));

  memcpy(dst, src, reinterpret_cast<uint64_t>(&src->i.i_ext) - reinterpret_cast<uint64_t>(&src->i));
  dst->i.i_size = 0;
  dst->i.i_blocks = 1;
  dst->i.i_links = 1;
  dst->i.i_xattr_nid = 0;

  new_ni = old_ni;
  new_ni.ino = ino;

  SetNodeAddr(new_ni, kNewAddr);
  fs_->IncValidInodeCount();

  F2fsPutPage(ipage, 1);
  return ZX_OK;
}

zx_status_t NodeManager::RestoreNodeSummary(F2fs &fs, uint32_t segno, SummaryBlock &sum) {
  Node *rn;
  Summary *sum_entry;
  Page *page = nullptr;
  block_t addr;
  int i, last_offset;
  SbInfo &sbi = fs.GetSbInfo();

  // scan the node segment
  last_offset = sbi.blocks_per_seg;
  addr = StartBlock(&sbi, segno);
  sum_entry = &sum.entries[0];

#if 0  // porting needed
  // alloc temporal page for read node
  // page = alloc_page(GFP_NOFS | __GFP_ZERO);
#endif
  page = GrabCachePage(nullptr, NodeIno(&sbi), addr);
  if (page == nullptr)
    return ZX_ERR_NO_MEMORY;
#if 0  // porting needed
  // lock_page(page);
#endif

  for (i = 0; i < last_offset; i++, sum_entry++) {
    if (VnodeF2fs::Readpage(&fs, page, addr, kReadSync)) {
      break;
    }

    rn = static_cast<Node *>(PageAddress(page));
    sum_entry->nid = rn->footer.nid;
    sum_entry->version = 0;
    sum_entry->ofs_in_node = 0;
    addr++;

#if 0  // porting needed
    // In order to read next node page,
    // we must clear PageUptodate flag.
    // ClearPageUptodate(page);
#endif
  }
#if 0  // porting needed
  // unlock_page(page);
  //__free_pages(page, 0);
#endif
  F2fsPutPage(page, 1);
  return ZX_OK;
}

bool NodeManager::FlushNatsInJournal() {
  CursegInfo *curseg = fs_->Segmgr().CURSEG_I(sbi_, CursegType::kCursegHotData);
  SummaryBlock *sum = curseg->sum_blk;
  int i;

  fbl::AutoLock curseg_lock(&curseg->curseg_mutex);

  {
    fs::SharedLock nat_lock(nat_tree_lock_);
    size_t dirty_nat_cnt = dirty_nat_list_.size_slow();
    if ((NatsInCursum(sum) + dirty_nat_cnt) <= kNatJournalEntries) {
      return false;
    }
  }

  for (i = 0; i < NatsInCursum(sum); i++) {
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
  CursegInfo *curseg = fs_->Segmgr().CURSEG_I(sbi_, CursegType::kCursegHotData);
  SummaryBlock *sum = curseg->sum_blk;
  Page *page = nullptr;
  NatBlock *nat_blk = nullptr;
  nid_t start_nid = 0, end_nid = 0;
  bool flushed;

  flushed = FlushNatsInJournal();

#if 0  // porting needed
  //	if (!flushed)
#endif
  fbl::AutoLock curseg_lock(&curseg->curseg_mutex);

  // 1) flush dirty nat caches
  {
    std::lock_guard nat_lock(nat_tree_lock_);
    for (auto iter = dirty_nat_list_.begin(); iter != dirty_nat_list_.end();) {
      nid_t nid;
      RawNatEntry raw_ne;
      int offset = -1;
      __UNUSED block_t old_blkaddr, new_blkaddr;

      NatEntry *cache_entry = iter.CopyPointer();
      iter++;

      nid = cache_entry->GetNid();

      if (cache_entry->GetBlockAddress() == kNewAddr)
        continue;

      if (!flushed) {
        // if there is room for nat enries in curseg->sumpage
        offset = fs_->Segmgr().LookupJournalInCursum(sum, JournalType::kNatJournal, nid, 1);
      }

      if (offset >= 0) {  // flush to journal
        raw_ne = NatInJournal(sum, offset);
        old_blkaddr = LeToCpu(raw_ne.block_addr);
      } else {  // flush to NAT block
        if (!page || (start_nid > nid || nid > end_nid)) {
          if (page) {
#if 0  // porting needed
       // set_page_dirty(page, fs_);
#endif
            FlushDirtyMetaPage(fs_, page);
            F2fsPutPage(page, 1);
            page = nullptr;
          }
          start_nid = StartNid(nid);
          end_nid = start_nid + kNatEntryPerBlock - 1;

          // get nat block with dirty flag, increased reference
          // count, mapped and lock
          page = GetNextNatPage(start_nid);
          nat_blk = static_cast<NatBlock *>(PageAddress(page));
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
#if 0  // porting needed
  //	if (!flushed)
#endif

#if 0  // porting needed
  // set_page_dirty(page, fs_);
#endif
  FlushDirtyMetaPage(fs_, page);
  F2fsPutPage(page, 1);

  // 2) shrink nat caches if necessary
  TryToFreeNats(nat_entries_count_ - kNmWoutThreshold);
}

zx_status_t NodeManager::InitNodeManager() {
  const SuperBlock *sb_raw = RawSuper(sbi_);
  uint8_t *version_bitmap;
  uint32_t nat_segs, nat_blocks;

  nat_blkaddr_ = LeToCpu(sb_raw->nat_blkaddr);
  // segment_count_nat includes pair segment so divide to 2
  nat_segs = LeToCpu(sb_raw->segment_count_nat) >> 1;
  nat_blocks = nat_segs << LeToCpu(sb_raw->log_blocks_per_seg);
  max_nid_ = kNatEntryPerBlock * nat_blocks;
  free_nid_count_ = 0;
  nat_entries_count_ = 0;

  list_initialize(&free_nid_list_);

  nat_bitmap_size_ = BitmapSize(sbi_, MetaBitmap::kNatBitmap);
  init_scan_nid_ = LeToCpu(sbi_->ckpt->next_free_nid);
  next_scan_nid_ = LeToCpu(sbi_->ckpt->next_free_nid);

  nat_bitmap_ = std::make_unique<uint8_t[]>(nat_bitmap_size_);
  memset(nat_bitmap_.get(), 0, nat_bitmap_size_);
  nat_prev_bitmap_ = std::make_unique<uint8_t[]>(nat_bitmap_size_);
  memset(nat_prev_bitmap_.get(), 0, nat_bitmap_size_);

  if (!nat_bitmap_)
    return ZX_ERR_NO_MEMORY;
  version_bitmap = static_cast<uint8_t *>(BitmapPrt(sbi_, MetaBitmap::kNatBitmap));
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
      uint32_t idx;
      for (idx = 0; idx < found; idx++) {
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
  ;
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

}  // namespace f2fs
