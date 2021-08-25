// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/f2fs/f2fs.h"

namespace f2fs {

inline void SetNatCacheDirty(NmInfo *nm_i, NatEntry *ne) {
  list_move_tail(&nm_i->dirty_nat_entries, &ne->list);
}

inline void ClearNatCacheDirty(NmInfo *nm_i, NatEntry *ne) {
  list_move_tail(&nm_i->nat_entries, &ne->list);
}

inline void NodeMgr::NodeInfoFromRawNat(NodeInfo *ni, RawNatEntry *raw_ne) {
  ni->ino = LeToCpu(raw_ne->ino);
  ni->blk_addr = LeToCpu(raw_ne->block_addr);
  ni->version = raw_ne->version;
}

inline bool NodeMgr::IncValidNodeCount(SbInfo *sbi, VnodeF2fs *vnode, uint32_t count) {
  block_t valid_block_count;
  uint32_t ValidNodeCount;

  fbl::AutoLock stat_lock(&sbi->stat_lock);

  valid_block_count = sbi->total_valid_block_count + static_cast<block_t>(count);
  sbi->alloc_valid_block_count += static_cast<block_t>(count);
  ValidNodeCount = sbi->total_valid_node_count + count;

  if (valid_block_count > sbi->user_block_count) {
    return false;
  }

  if (ValidNodeCount > sbi->total_node_count) {
    return false;
  }

  if (vnode)
    vnode->IncBlocks(count);
  sbi->total_valid_node_count = ValidNodeCount;
  sbi->total_valid_block_count = valid_block_count;

  return true;
}

/*
 * inline functions
 */
zx_status_t NodeMgr::NextFreeNid(nid_t *nid) {
  SbInfo &sbi = fs_->GetSbInfo();
  NmInfo *nm_i = GetNmInfo(&sbi);
  FreeNid *fnid;

  if (nm_i->fcnt <= 0)
    return ZX_ERR_OUT_OF_RANGE;

  std::lock_guard free_nid_lock(nm_i->free_nid_list_lock);
  fnid = containerof(nm_i->free_nid_list.next, FreeNid, list);
  *nid = fnid->nid;
  return ZX_OK;
}

void NodeMgr::GetNatBitmap(void *addr) {
  SbInfo &sbi = fs_->GetSbInfo();
  NmInfo *nm_i = GetNmInfo(&sbi);
  memcpy(addr, nm_i->nat_bitmap, nm_i->bitmap_size);
  memcpy(nm_i->nat_prev_bitmap, nm_i->nat_bitmap, nm_i->bitmap_size);
}

inline pgoff_t NodeMgr::CurrentNatAddr(nid_t start) {
  SbInfo &sbi = fs_->GetSbInfo();
  NmInfo *nm_i = GetNmInfo(&sbi);
  pgoff_t block_off;
  pgoff_t block_addr;
  int seg_off;

  block_off = NatBlockOffset(start);
  seg_off = block_off >> sbi.log_blocks_per_seg;

  block_addr = static_cast<pgoff_t>(nm_i->nat_blkaddr + (seg_off << sbi.log_blocks_per_seg << 1) +
                                    (block_off & ((1 << sbi.log_blocks_per_seg) - 1)));

  if (TestValidBitmap(block_off, nm_i->nat_bitmap))
    block_addr += sbi.blocks_per_seg;

  return block_addr;
}

inline bool NodeMgr::IsUpdatedNatPage(nid_t start) {
  SbInfo &sbi = fs_->GetSbInfo();
  NmInfo *nm_i = GetNmInfo(&sbi);
  pgoff_t block_off;

  block_off = NatBlockOffset(start);

  return (TestValidBitmap(block_off, nm_i->nat_bitmap) ^
          TestValidBitmap(block_off, nm_i->nat_prev_bitmap));
}

inline pgoff_t NodeMgr::NextNatAddr(pgoff_t block_addr) {
  SbInfo &sbi = fs_->GetSbInfo();
  NmInfo *nm_i = GetNmInfo(&sbi);

  block_addr -= nm_i->nat_blkaddr;
  if ((block_addr >> sbi.log_blocks_per_seg) % 2)
    block_addr -= sbi.blocks_per_seg;
  else
    block_addr += sbi.blocks_per_seg;

  return block_addr + nm_i->nat_blkaddr;
}

inline void NodeMgr::SetToNextNat(NmInfo *nm_i, nid_t start_nid) {
  uint32_t block_off = NatBlockOffset(start_nid);

  if (TestValidBitmap(block_off, nm_i->nat_bitmap))
    ClearValidBitmap(block_off, nm_i->nat_bitmap);
  else
    SetValidBitmap(block_off, nm_i->nat_bitmap);
}

inline void NodeMgr::FillNodeFooter(Page *page, nid_t nid, nid_t ino, uint32_t ofs, bool reset) {
  void *kaddr = PageAddress(page);
  Node *rn = static_cast<Node *>(kaddr);
  if (reset)
    memset(rn, 0, sizeof(*rn));
  rn->footer.nid = CpuToLe(nid);
  rn->footer.ino = CpuToLe(ino);
  rn->footer.flag = CpuToLe(ofs << static_cast<int>(BitShift::kOffsetBitShift));
}

void NodeMgr::CopyNodeFooter(Page *dst, Page *src) {
  void *src_addr = PageAddress(src);
  void *dst_addr = PageAddress(dst);
  Node *src_rn = static_cast<Node *>(src_addr);
  Node *dst_rn = static_cast<Node *>(dst_addr);
  memcpy(&dst_rn->footer, &src_rn->footer, sizeof(NodeFooter));
}

void NodeMgr::FillNodeFooterBlkaddr(Page *page, block_t blkaddr) {
  // TODO: IMPL
  // SbInfo *sbi = F2FS_SB(page->mapping->host->i_sb);
  SbInfo &sbi = fs_->GetSbInfo();
  Checkpoint *ckpt = GetCheckpoint(&sbi);
  void *kaddr = PageAddress(page);
  Node *rn = static_cast<Node *>(kaddr);
  rn->footer.cp_ver = ckpt->checkpoint_ver;
  rn->footer.next_blkaddr = blkaddr;
}

inline nid_t NodeMgr::InoOfNode(Page *node_page) {
  void *kaddr = PageAddress(node_page);
  Node *rn = static_cast<Node *>(kaddr);
  return LeToCpu(rn->footer.ino);
}

inline nid_t NodeMgr::NidOfNode(Page *node_page) {
  void *kaddr = PageAddress(node_page);
  Node *rn = static_cast<Node *>(kaddr);
  return LeToCpu(rn->footer.nid);
}

uint32_t NodeMgr::OfsOfNode(Page *node_page) {
  void *kaddr = PageAddress(node_page);
  Node *rn = static_cast<Node *>(kaddr);
  uint32_t flag = LeToCpu(rn->footer.flag);
  return flag >> static_cast<int>(BitShift::kOffsetBitShift);
}

uint64_t NodeMgr::CpverOfNode(Page *node_page) {
  void *kaddr = PageAddress(node_page);
  Node *rn = static_cast<Node *>(kaddr);
  return LeToCpu(rn->footer.cp_ver);
}

block_t NodeMgr::NextBlkaddrOfNode(Page *node_page) {
  void *kaddr = PageAddress(node_page);
  Node *rn = static_cast<Node *>(kaddr);
  return LeToCpu(rn->footer.next_blkaddr);
}

/*
 * f2fs assigns the following node offsets described as (num).
 * N = kNidsPerBlock
 *
 *  Inode block (0)
 *    |- direct node (1)
 *    |- direct node (2)
 *    |- indirect node (3)
 *    |            `- direct node (4 => 4 + N - 1)
 *    |- indirect node (4 + N)
 *    |            `- direct node (5 + N => 5 + 2N - 1)
 *    `- double indirect node (5 + 2N)
 *                 `- indirect node (6 + 2N)
 *                       `- direct node (x(N + 1))
 */
bool NodeMgr::IS_DNODE(Page *node_page) {
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

inline void NodeMgr::SetNid(Page *p, int off, nid_t nid, bool i) {
  Node *rn = static_cast<Node *>(PageAddress(p));

  WaitOnPageWriteback(p);

  if (i) {
    rn->i.i_nid[off - kNodeDir1Block] = CpuToLe(nid);
  } else {
    rn->in.nid[off] = CpuToLe(nid);
  }

#if 0  // porting needed
  // set_page_dirty(p);
#endif
  FlushDirtyNodePage(fs_, p);
}

inline nid_t NodeMgr::GetNid(Page *p, int off, bool i) {
  Node *rn = static_cast<Node *>(PageAddress(p));
  if (i)
    return LeToCpu(rn->i.i_nid[off - kNodeDir1Block]);
  return LeToCpu(rn->in.nid[off]);
}

/*
 * Coldness identification:
 *  - Mark cold files in InodeInfo
 *  - Mark cold node blocks in their node footer
 *  - Mark cold data pages in page cache
 */
bool NodeMgr::IsColdFile(VnodeF2fs *vnode) { return (vnode->IsAdviseSet(FAdvise::kCold) != 0); }

int NodeMgr::IsColdData(Page *page) {
#if 0  // porting needed
  // return PageChecked(page);
#endif
  return 0;
}

#if 0  // porting needed
inline void NodeMgr::SetColdData(Page *page) {
  // SetPageChecked(page);
}
#endif

void NodeMgr::ClearColdData(Page *page) {
#if 0  // porting needed
  // ClearPageChecked(page);
#endif
}

int NodeMgr::IsColdNode(Page *page) {
  void *kaddr = PageAddress(page);
  Node *rn = static_cast<Node *>(kaddr);
  uint32_t flag = LeToCpu(rn->footer.flag);
  return flag & (0x1 << static_cast<int>(BitShift::kColdBitShift));
}

uint8_t NodeMgr::IsFsyncDnode(Page *page) {
  void *kaddr = PageAddress(page);
  Node *rn = static_cast<Node *>(kaddr);
  uint32_t flag = LeToCpu(rn->footer.flag);
  return flag & (0x1 << static_cast<int>(BitShift::kFsyncBitShift));
}

uint8_t NodeMgr::IsDentDnode(Page *page) {
  void *kaddr = PageAddress(page);
  Node *rn = static_cast<Node *>(kaddr);
  uint32_t flag = LeToCpu(rn->footer.flag);
  return flag & (0x1 << static_cast<int>(BitShift::kDentBitShift));
}

inline void NodeMgr::SetColdNode(VnodeF2fs *vnode, Page *page) {
  Node *rn = static_cast<Node *>(PageAddress(page));
  uint32_t flag = LeToCpu(rn->footer.flag);

  if (vnode->IsDir())
    flag &= ~(0x1 << static_cast<int>(BitShift::kColdBitShift));
  else
    flag |= (0x1 << static_cast<int>(BitShift::kColdBitShift));
  rn->footer.flag = CpuToLe(flag);
}

void NodeMgr::SetFsyncMark(Page *page, int mark) {
  void *kaddr = PageAddress(page);
  Node *rn = static_cast<Node *>(kaddr);
  uint32_t flag = LeToCpu(rn->footer.flag);
  if (mark)
    flag |= (0x1 << static_cast<int>(BitShift::kFsyncBitShift));
  else
    flag &= ~(0x1 << static_cast<int>(BitShift::kFsyncBitShift));
  rn->footer.flag = CpuToLe(flag);
}

void NodeMgr::SetDentryMark(Page *page, int mark) {
  void *kaddr = PageAddress(page);
  Node *rn = static_cast<Node *>(kaddr);
  uint32_t flag = LeToCpu(rn->footer.flag);
  if (mark)
    flag |= (0x1 << static_cast<int>(BitShift::kDentBitShift));
  else
    flag &= ~(0x1 << static_cast<int>(BitShift::kDentBitShift));
  rn->footer.flag = CpuToLe(flag);
}

inline void NodeMgr::DecValidNodeCount(SbInfo *sbi, VnodeF2fs *vnode, uint32_t count) {
  fbl::AutoLock stat_lock(&sbi->stat_lock);

  // TODO: IMPL
  ZX_ASSERT(!(sbi->total_valid_block_count < count));
  ZX_ASSERT(!(sbi->total_valid_node_count < count));

  vnode->DecBlocks(count);
  sbi->total_valid_node_count -= count;
  sbi->total_valid_block_count -= count;
}

/*
 *  Functions
 */

NodeMgr::NodeMgr(F2fs *fs) : fs_(fs){};

void NodeMgr::ClearNodePageDirty(Page *page) {
  SbInfo &sbi = fs_->GetSbInfo();
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
    DecPageCount(&sbi, CountType::kDirtyNodes);
  }
  ClearPageUptodate(page);
}

Page *NodeMgr::GetCurrentNatPage(nid_t nid) {
  pgoff_t index = CurrentNatAddr(nid);
  return fs_->GetMetaPage(index);
}

Page *NodeMgr::GetNextNatPage(nid_t nid) {
  Page *src_page;
  Page *dst_page;
  pgoff_t src_off;
  pgoff_t dst_off;
  void *src_addr;
  void *dst_addr;
  SbInfo &sbi = fs_->GetSbInfo();
  NmInfo *nm_i = GetNmInfo(&sbi);

  src_off = CurrentNatAddr(nid);
  dst_off = NextNatAddr(src_off);

  /* get current nat block page with lock */
  src_page = fs_->GetMetaPage(src_off);

  /* Dirty src_page means that it is already the new target NAT page. */
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

  SetToNextNat(nm_i, nid);

  return dst_page;
}

/**
 * Readahead NAT pages
 */
void NodeMgr::RaNatPages(nid_t nid) {
  // address_space *mapping = sbi_->meta_inode->i_mapping;
  SbInfo &sbi = fs_->GetSbInfo();
  NmInfo *nm_i = GetNmInfo(&sbi);
  Page *page;
  pgoff_t index;
  int i;

  for (i = 0; i < kFreeNidPages; i++, nid += kNatEntryPerBlock) {
    if (nid >= nm_i->max_nid)
      nid = 0;
    index = CurrentNatAddr(nid);

    page = GrabCachePage(nullptr, MetaIno(&sbi), index);
    if (!page)
      continue;
    if (VnodeF2fs::Readpage(fs_, page, index, 0 /*READ*/)) {
      F2fsPutPage(page, 1);
      continue;
    }
#if 0  // porting needed
    // page_cache_release(page);
#endif
    F2fsPutPage(page, 1);
  }
}

NatEntry *NodeMgr::LookupNatCache(NmInfo *nm_i, nid_t n) {
  // TODO: IMPL

  // TODO: need to be modified to use radix tree
  list_node_t *cur, *next;
  list_for_every_safe(&nm_i->dirty_nat_entries, cur, next) {
    NatEntry *e = containerof(cur, NatEntry, list);

    if (NatGetNid(e) == n) {
      return e;
    }
  }

  list_for_every_safe(&nm_i->nat_entries, cur, next) {
    NatEntry *e = containerof(cur, NatEntry, list);

    if (NatGetNid(e) == n) {
      return e;
    }
  }
#if 0  // porting needed
  // return radix_tree_lookup(&nm_i->nat_root, n);
#endif
  return nullptr;
}

uint32_t NodeMgr::GangLookupNatCache(NmInfo *nm_i, nid_t start, uint32_t nr, NatEntry **ep) {
  // TODO: IMPL

  // TODO: need to be modified to use radix tree
  uint32_t ret = 0;

  for (uint32_t i = 0; i < nr; i++) {
    nid_t cur_nid = start + i;
    ep[ret] = LookupNatCache(nm_i, cur_nid);
    if (ep[ret]) {
      if (++ret == nr) {
        break;
      }
    }
  }

  return ret;
#if 0  // porting needed
  //	return radix_tree_gang_lookup(&nm_i->nat_root, (void **)ep, start, nr);
  //   return 0;
#endif
}

void NodeMgr::DelFromNatCache(NmInfo *nm_i, NatEntry *e) {
#if 0  // porting needed
  // radix_tree_delete(&nm_i->nat_root, NatGetNid(e));
#endif
  list_delete(&e->list);
  nm_i->nat_cnt--;
#if 0  // porting needed
  // kmem_cache_free(NatEntry_slab, e);
#endif
  delete e;
}

int NodeMgr::IsCheckpointedNode(nid_t nid) {
  SbInfo &sbi = fs_->GetSbInfo();
  NmInfo *nm_i = GetNmInfo(&sbi);
  NatEntry *e;
  int is_cp = 1;

  fs::SharedLock nat_lock(nm_i->nat_tree_lock);
  e = LookupNatCache(nm_i, nid);
  if (e && !e->checkpointed)
    is_cp = 0;
  return is_cp;
}

NatEntry *NodeMgr::GrabNatEntry(NmInfo *nm_i, nid_t nid) {
  NatEntry *new_entry;

#if 0  // porting needed (kmem_cache_alloc)
  // new_entry = kmem_cache_alloc(NatEntry_slab, GFP_ATOMIC);
#endif
  new_entry = new NatEntry;
  if (!new_entry)
    return nullptr;
#if 0  // porting needed
  // if (radix_tree_insert(&nm_i->nat_root, nid, new_entry)) {
  // delete new_entry;
  // 	return NULL;
  // }
#endif
  memset(new_entry, 0, sizeof(NatEntry));
  NatSetNid(new_entry, nid);
  list_add_tail(&nm_i->nat_entries, &new_entry->list);
  nm_i->nat_cnt++;
  return new_entry;
}

void NodeMgr::CacheNatEntry(NmInfo *nm_i, nid_t nid, RawNatEntry *ne) {
  NatEntry *e;
retry:
  std::lock_guard lock(nm_i->nat_tree_lock);
  e = LookupNatCache(nm_i, nid);
  if (!e) {
    e = GrabNatEntry(nm_i, nid);
    if (!e) {
      goto retry;
    }
    NatSetBlkaddr(e, LeToCpu(ne->block_addr));
    NatSetIno(e, LeToCpu(ne->ino));
    NatSetVersion(e, ne->version);
    e->checkpointed = true;
  }
}

void NodeMgr::SetNodeAddr(NodeInfo *ni, block_t new_blkaddr) {
  SbInfo &sbi = fs_->GetSbInfo();
  NmInfo *nm_i = GetNmInfo(&sbi);
  NatEntry *e;
retry:
  std::lock_guard nat_lock(nm_i->nat_tree_lock);
  e = LookupNatCache(nm_i, ni->nid);
  if (!e) {
    e = GrabNatEntry(nm_i, ni->nid);
    if (!e) {
      goto retry;
    }
    e->ni = *ni;
    e->checkpointed = true;
    ZX_ASSERT(ni->blk_addr != kNewAddr);
  } else if (new_blkaddr == kNewAddr) {
    /*
     * when nid is reallocated,
     * previous nat entry can be remained in nat cache.
     * So, reinitialize it with new information.
     */
    e->ni = *ni;
    ZX_ASSERT(ni->blk_addr == kNullAddr);
  }

  if (new_blkaddr == kNewAddr)
    e->checkpointed = false;

  /* sanity check */
  ZX_ASSERT(!(NatGetBlkaddr(e) != ni->blk_addr));
  ZX_ASSERT(!(NatGetBlkaddr(e) == kNullAddr && new_blkaddr == kNullAddr));
  ZX_ASSERT(!(NatGetBlkaddr(e) == kNewAddr && new_blkaddr == kNewAddr));
  ZX_ASSERT(
      !(NatGetBlkaddr(e) != kNewAddr && NatGetBlkaddr(e) != kNullAddr && new_blkaddr == kNewAddr));

  /* increament version no as node is removed */
  if (NatGetBlkaddr(e) != kNewAddr && new_blkaddr == kNullAddr) {
    uint8_t version = NatGetVersion(e);
    NatSetVersion(e, IncNodeVersion(version));
  }

  /* change address */
  NatSetBlkaddr(e, new_blkaddr);
  SetNatCacheDirty(nm_i, e);
}

int NodeMgr::TryToFreeNats(int nr_shrink) {
  SbInfo &sbi = fs_->GetSbInfo();
  NmInfo *nm_i = GetNmInfo(&sbi);

  if (nm_i->nat_cnt < 2 * kNmWoutThreshold)
    return 0;

  std::lock_guard nat_lock(nm_i->nat_tree_lock);
  while (nr_shrink && !list_is_empty(&nm_i->nat_entries)) {
    NatEntry *ne;
    // ne = list_first_entry(&nm_i->nat_entries,
    //			NatEntry, list);
    ne = containerof((&nm_i->nat_entries)->next, NatEntry, list);
    DelFromNatCache(nm_i, ne);
    nr_shrink--;
  }
  return nr_shrink;
}

/**
 * This function returns always success
 */
void NodeMgr::GetNodeInfo(nid_t nid, NodeInfo *ni) {
  SbInfo &sbi = fs_->GetSbInfo();
  NmInfo *nm_i = GetNmInfo(&sbi);
  CursegInfo *curseg = SegMgr::CURSEG_I(&sbi, CursegType::kCursegHotData);
  SummaryBlock *sum = curseg->sum_blk;
  nid_t start_nid = StartNid(nid);
  NatBlock *nat_blk;
  Page *page = NULL;
  RawNatEntry ne;
  NatEntry *e;
  int i;

  ni->nid = nid;

  {
    /* Check nat cache */
    fs::SharedLock nat_lock(nm_i->nat_tree_lock);
    e = LookupNatCache(nm_i, nid);
    if (e) {
      ni->ino = NatGetIno(e);
      ni->blk_addr = NatGetBlkaddr(e);
      ni->version = NatGetVersion(e);
      return;
    }
  }

  {
    /* Check current segment summary */
    fbl::AutoLock curseg_lock(&curseg->curseg_mutex);
    i = SegMgr::LookupJournalInCursum(sum, JournalType::kNatJournal, nid, 0);
    if (i >= 0) {
      ne = NatInJournal(sum, i);
      NodeInfoFromRawNat(ni, &ne);
    }
  }
  if (i < 0) {
    /* Fill NodeInfo from nat page */
    page = GetCurrentNatPage(start_nid);
    nat_blk = static_cast<NatBlock *>(PageAddress(page));
    ne = nat_blk->entries[nid - start_nid];

    NodeInfoFromRawNat(ni, &ne);
    F2fsPutPage(page, 1);
  }
  /* cache nat entry */
  CacheNatEntry(GetNmInfo(&sbi), nid, &ne);
}

/**
 * The maximum depth is four.
 * Offset[0] will have raw inode offset.
 */
zx::status<int> NodeMgr::GetNodePath(long block, int offset[4], uint32_t noffset[4]) {
  const long direct_index = kAddrsPerInode;
  const long direct_blks = kAddrsPerBlock;
  const long dptrs_per_blk = kNidsPerBlock;
  const long indirect_blks = kAddrsPerBlock * kNidsPerBlock;
  const long dindirect_blks = indirect_blks * kNidsPerBlock;
  int n = 0;
  int level = 0;

  noffset[0] = 0;

  if (block < direct_index) {
    offset[n++] = block;
    level = 0;
    goto got;
  }
  block -= direct_index;
  if (block < direct_blks) {
    offset[n++] = kNodeDir1Block;
    noffset[n] = 1;
    offset[n++] = block;
    level = 1;
    goto got;
  }
  block -= direct_blks;
  if (block < direct_blks) {
    offset[n++] = kNodeDir2Block;
    noffset[n] = 2;
    offset[n++] = block;
    level = 1;
    goto got;
  }
  block -= direct_blks;
  if (block < indirect_blks) {
    offset[n++] = kNodeInd1Block;
    noffset[n] = 3;
    offset[n++] = block / direct_blks;
    noffset[n] = 4 + offset[n - 1];
    offset[n++] = block % direct_blks;
    level = 2;
    goto got;
  }
  block -= indirect_blks;
  if (block < indirect_blks) {
    offset[n++] = kNodeInd2Block;
    noffset[n] = 4 + dptrs_per_blk;
    offset[n++] = block / direct_blks;
    noffset[n] = 5 + dptrs_per_blk + offset[n - 1];
    offset[n++] = block % direct_blks;
    level = 2;
    goto got;
  }
  block -= indirect_blks;
  if (block < dindirect_blks) {
    offset[n++] = kNodeDIndBlock;
    noffset[n] = 5 + (dptrs_per_blk * 2);
    offset[n++] = block / indirect_blks;
    noffset[n] = 6 + (dptrs_per_blk * 2) + offset[n - 1] * (dptrs_per_blk + 1);
    offset[n++] = (block / direct_blks) % dptrs_per_blk;
    noffset[n] = 7 + (dptrs_per_blk * 2) + offset[n - 2] * (dptrs_per_blk + 1) + offset[n - 1];
    offset[n++] = block % direct_blks;
    level = 3;
    goto got;
  } else {
    zx::error(ZX_ERR_NOT_FOUND);
  }
got:
  return zx::ok(level);
}

// Caller should call f2fs_put_dnode(dn).
zx_status_t NodeMgr::GetDnodeOfData(DnodeOfData *dn, pgoff_t index, int ro) {
  Page *npage[4];
  Page *parent;
  int offset[4];
  uint32_t noffset[4];
  nid_t nids[4];
  int level, i;
  zx_status_t err = 0;

  auto node_path = GetNodePath(index, offset, noffset);
  if (node_path.is_error())
    return node_path.error_value();

  level = *node_path;

  nids[0] = dn->vnode->Ino();
  npage[0] = nullptr;
  err = GetNodePage(nids[0], &npage[0]);
  if (err)
    return err;

  parent = npage[0];
  nids[1] = GetNid(parent, offset[0], true);
  dn->inode_page = npage[0];
  dn->inode_page_locked = true;

  /* get indirect or direct nodes */
  for (i = 1; i <= level; i++) {
    bool done = false;

    if (!nids[i] && !ro) {
      /* alloc new node */
      if (!AllocNid(&(nids[i]))) {
        err = ZX_ERR_NO_SPACE;
        goto release_pages;
      }

      dn->nid = nids[i];
      npage[i] = nullptr;
      err = NewNodePage(dn, noffset[i], &npage[i]);
      if (err) {
        AllocNidFailed(nids[i]);
        goto release_pages;
      }

      SetNid(parent, offset[i - 1], nids[i], i == 1);
      AllocNidDone(nids[i]);
      done = true;
    } else if (ro && i == level && level > 1) {
#if 0  // porting needed
      // err = GetNodePageRa(parent, offset[i - 1], &npage[i]);
      // if (err) {
      // 	goto release_pages;
      // }
      // done = true;
#endif
    }
    if (i == 1) {
      dn->inode_page_locked = false;
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
        goto release_out;
      }
    }
    if (i < level) {
      parent = npage[i];
      nids[i + 1] = GetNid(parent, offset[i], false);
    }
  }
  dn->nid = nids[level];
  dn->ofs_in_node = offset[level];
  dn->node_page = npage[level];
  dn->data_blkaddr = DatablockAddr(dn->node_page, dn->ofs_in_node);

#ifdef F2FS_BU_DEBUG
  FX_LOGS(DEBUG) << "NodeMgr::GetDnodeOfData"
                 << ", dn->nid=" << dn->nid << ", dn->node_page=" << dn->node_page
                 << ", dn->ofs_in_node=" << dn->ofs_in_node
                 << ", dn->data_blkaddr=" << dn->data_blkaddr;
#endif
  return ZX_OK;

release_pages:
  F2fsPutPage(parent, 1);
  if (i > 1)
    F2fsPutPage(npage[0], 0);
release_out:
  dn->inode_page = nullptr;
  dn->node_page = nullptr;
  return err;
}

void NodeMgr::TruncateNode(DnodeOfData *dn) {
  SbInfo &sbi = fs_->GetSbInfo();
  NodeInfo ni;

  GetNodeInfo(dn->nid, &ni);
  ZX_ASSERT(ni.blk_addr != kNullAddr);

  if (ni.blk_addr != kNullAddr)
    fs_->Segmgr().InvalidateBlocks(ni.blk_addr);

  /* Deallocate node address */
  DecValidNodeCount(&sbi, dn->vnode, 1);
  SetNodeAddr(&ni, kNullAddr);

  if (dn->nid == dn->vnode->Ino()) {
    fs_->RemoveOrphanInode(dn->nid);
    fs_->DecValidInodeCount();
  } else {
    SyncInodePage(dn);
  }

  ClearNodePageDirty(dn->node_page);
  SetSbDirt(&sbi);

  F2fsPutPage(dn->node_page, 1);
  dn->node_page = nullptr;
}

zx_status_t NodeMgr::TruncateDnode(DnodeOfData *dn) {
  Page *page = nullptr;
  zx_status_t err = 0;

  if (dn->nid == 0)
    return 1;

  /* get direct node */
  err = fs_->Nodemgr().GetNodePage(dn->nid, &page);
  if (err && err == ZX_ERR_NOT_FOUND)
    return 1;
  else if (err)
    return err;

  /* Make DnodeOfData for parameter */
  dn->node_page = page;
  dn->ofs_in_node = 0;
  dn->vnode->TruncateDataBlocks(dn);
  TruncateNode(dn);
  return 1;
}

zx_status_t NodeMgr::TruncateNodes(DnodeOfData *dn, uint32_t nofs, int ofs, int depth) {
  DnodeOfData rdn = *dn;
  Page *page = nullptr;
  Node *rn;
  nid_t child_nid;
  uint32_t child_nofs;
  int freed = 0;
  int i, ret;
  zx_status_t err = 0;

  if (dn->nid == 0)
    return kNidsPerBlock + 1;

  err = fs_->Nodemgr().GetNodePage(dn->nid, &page);
  if (err)
    return err;

  rn = (Node *)PageAddress(page);
  if (depth < 3) {
    for (i = ofs; i < kNidsPerBlock; i++, freed++) {
      child_nid = LeToCpu(rn->in.nid[i]);
      if (child_nid == 0)
        continue;
      rdn.nid = child_nid;
      ret = TruncateDnode(&rdn);
      if (ret < 0)
        goto out_err;
      SetNid(page, i, 0, false);
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
      ret = TruncateNodes(&rdn, child_nofs, 0, depth - 1);
      if (ret == (kNidsPerBlock + 1)) {
        SetNid(page, i, 0, false);
        child_nofs += ret;
      } else if (ret < 0 && ret != ZX_ERR_NOT_FOUND) {
        goto out_err;
      }
    }
    freed = child_nofs;
  }

  if (!ofs) {
    /* remove current indirect node */
    dn->node_page = page;
    TruncateNode(dn);
    freed++;
  } else {
    F2fsPutPage(page, 1);
  }
  return freed;

out_err:
  F2fsPutPage(page, 1);
  return ret;
}

zx_status_t NodeMgr::TruncatePartialNodes(DnodeOfData *dn, Inode *ri, int *offset, int depth) {
  Page *pages[2];
  nid_t nid[3];
  nid_t child_nid;
  zx_status_t err = 0;
  int i;
  int idx = depth - 2;

  nid[0] = LeToCpu(ri->i_nid[offset[0] - kNodeDir1Block]);
  if (!nid[0])
    return ZX_OK;

  /* get indirect nodes in the path */
  for (i = 0; i < depth - 1; i++) {
    /* refernece count'll be increased */
    pages[i] = nullptr;
    err = fs_->Nodemgr().GetNodePage(nid[i], &pages[i]);
    if (err) {
      depth = i + 1;
      goto fail;
    }
    nid[i + 1] = GetNid(pages[i], offset[i + 1], false);
  }

  /* free direct nodes linked to a partial indirect node */
  for (i = offset[depth - 1]; i < kNidsPerBlock; i++) {
    child_nid = GetNid(pages[idx], i, false);
    if (!child_nid)
      continue;
    dn->nid = child_nid;
    err = TruncateDnode(dn);
    if (err < 0)
      goto fail;
    SetNid(pages[idx], i, 0, false);
  }

  if (offset[depth - 1] == 0) {
    dn->node_page = pages[idx];
    dn->nid = nid[idx];
    TruncateNode(dn);
  } else {
    F2fsPutPage(pages[idx], 1);
  }
  offset[idx]++;
  offset[depth - 1] = 0;
fail:
  for (i = depth - 3; i >= 0; i--)
    F2fsPutPage(pages[i], 1);
  return err;
}

/**
 * All the block addresses of data and nodes should be nullified.
 */
zx_status_t NodeMgr::TruncateInodeBlocks(VnodeF2fs *vnode, pgoff_t from) {
  int cont = 1;
  int level, offset[4], noffset[4];
  uint32_t nofs;
  Node *rn;
  DnodeOfData dn;
  Page *page = nullptr;
  zx_status_t err = 0;

  auto node_path = GetNodePath(from, offset, reinterpret_cast<uint32_t *>(noffset));
  if (node_path.is_error())
    return node_path.error_value();

  level = *node_path;

  err = GetNodePage(vnode->Ino(), &page);
  if (err)
    return err;

  SetNewDnode(&dn, vnode, page, nullptr, 0);
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
      if (!offset[level - 1])
        goto skip_partial;
      err = TruncatePartialNodes(&dn, &rn->i, offset, level);
      if (err < 0 && err != ZX_ERR_NOT_FOUND)
        goto fail;
      nofs += 1 + kNidsPerBlock;
      break;
    case 3:
      nofs = 5 + 2 * kNidsPerBlock;
      if (!offset[level - 1])
        goto skip_partial;
      err = TruncatePartialNodes(&dn, &rn->i, offset, level);
      if (err < 0 && err != ZX_ERR_NOT_FOUND)
        goto fail;
      break;
    default:
      ZX_ASSERT(0);
  }

skip_partial:
  while (cont) {
    dn.nid = LeToCpu(rn->i.i_nid[offset[0] - kNodeDir1Block]);
    switch (offset[0]) {
      case kNodeDir1Block:
      case kNodeDir2Block:
        err = TruncateDnode(&dn);
        break;

      case kNodeInd1Block:
      case kNodeInd2Block:
        err = TruncateNodes(&dn, nofs, offset[1], 2);
        break;

      case kNodeDIndBlock:
        err = TruncateNodes(&dn, nofs, offset[1], 3);
        cont = 0;
        break;

      default:
        ZX_ASSERT(0);
    }
    if (err < 0 && err != ZX_ERR_NOT_FOUND)
      goto fail;
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
fail:
  F2fsPutPage(page, 0);
  return err > 0 ? 0 : err;
}

zx_status_t NodeMgr::RemoveInodePage(VnodeF2fs *vnode) {
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

    SetNewDnode(&dn, vnode, page, npage, nid);
    dn.inode_page_locked = true;
    TruncateNode(&dn);
  }
  if (vnode->GetBlocks() == 1) {
    /* inernally call f2fs_put_page() */
    SetNewDnode(&dn, vnode, page, page, ino);
    TruncateNode(&dn);
  } else if (vnode->GetBlocks() == 0) {
    NodeInfo ni;
    GetNodeInfo(vnode->Ino(), &ni);

    /* called after f2fs_new_inode() is failed */
    ZX_ASSERT(ni.blk_addr == kNullAddr);
    F2fsPutPage(page, 1);
  } else {
    ZX_ASSERT(0);
  }
  return ZX_OK;
}

zx_status_t NodeMgr::NewInodePage(Dir *parent, VnodeF2fs *child) {
  Page *page = nullptr;
  DnodeOfData dn;
  zx_status_t err = 0;

  /* allocate inode page for new inode */
  SetNewDnode(&dn, child, nullptr, nullptr, child->Ino());
  err = NewNodePage(&dn, 0, &page);
  parent->InitDentInode(child, page);

  if (err)
    return err;
  F2fsPutPage(page, 1);
  return ZX_OK;
}

zx_status_t NodeMgr::NewNodePage(DnodeOfData *dn, uint32_t ofs, Page **out) {
  SbInfo &sbi = fs_->GetSbInfo();
  //[[maybe_unused]] address_space *mapping = sbi.node_inode->i_mapping;
  NodeInfo old_ni, new_ni;
  Page *page = nullptr;
  int err;

  if (dn->vnode->TestFlag(InodeInfoFlag::kNoAlloc))
    return ZX_ERR_ACCESS_DENIED;

  page = GrabCachePage(nullptr, NodeIno(&sbi), dn->nid);
  if (!page)
    return ZX_ERR_NO_MEMORY;

  GetNodeInfo(dn->nid, &old_ni);

  SetPageUptodate(page);
  FillNodeFooter(page, dn->nid, dn->vnode->Ino(), ofs, true);

  /* Reinitialize old_ni with new node page */
  ZX_ASSERT(old_ni.blk_addr == kNullAddr);
  new_ni = old_ni;
  new_ni.ino = dn->vnode->Ino();

  if (!IncValidNodeCount(&sbi, dn->vnode, 1)) {
    err = ZX_ERR_NO_SPACE;
    goto fail;
  }
  SetNodeAddr(&new_ni, kNewAddr);

  dn->node_page = page;
  SyncInodePage(dn);

#if 0  // porting needed
  //   set_page_dirty(page);
#endif
  SetColdNode(dn->vnode, page);
  FlushDirtyNodePage(fs_, page);
  if (ofs == 0)
    fs_->IncValidInodeCount();

  *out = page;
  return ZX_OK;

fail:
  F2fsPutPage(page, 1);
  return err;
}

zx_status_t NodeMgr::ReadNodePage(Page *page, nid_t nid, int type) {
  NodeInfo ni;

  GetNodeInfo(nid, &ni);

  if (ni.blk_addr == kNullAddr)
    return ZX_ERR_NOT_FOUND;

  if (ni.blk_addr == kNewAddr) {
#ifdef F2FS_BU_DEBUG
    FX_LOGS(DEBUG) << "NodeMgr::ReadNodePage, Read New address...";
#endif
    return ZX_OK;
  }

  return VnodeF2fs::Readpage(fs_, page, ni.blk_addr, type);
}

/**
 * Readahead a node page
 */
#if 0  // porting needed
void NodeMgr::RaNodePage(nid_t nid) {
  // TODO: IMPL Read ahead
}
#endif

zx_status_t NodeMgr::GetNodePage(pgoff_t nid, Page **out) {
  int err;
  Page *page = nullptr;
  SbInfo &sbi = fs_->GetSbInfo();
#if 0  // porting needed
  // address_space *mapping = sbi_->node_inode->i_mapping;
#endif

  page = GrabCachePage(nullptr, NodeIno(&sbi), nid);
  if (!page)
    return ZX_ERR_NO_MEMORY;

  err = ReadNodePage(page, nid, kReadSync);
  if (err) {
    F2fsPutPage(page, 1);
    return err;
  }

  ZX_ASSERT(nid == NidOfNode(page));
#if 0  // porting needed
  // mark_page_accessed(page);
#endif
  *out = page;
  return ZX_OK;
}

/**
 * Return a locked page for the desired node page.
 * And, readahead kMaxRaNode number of node pages.
 */
Page *NodeMgr::GetNodePageRa(Page *parent, int start) {
  // TODO: IMPL Read ahead
  return nullptr;
}

void NodeMgr::SyncInodePage(DnodeOfData *dn) {
  if (dn->vnode->GetNlink())
    dn->vnode->MarkInodeDirty();
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

int NodeMgr::SyncNodePages(nid_t ino, WritebackControl *wbc) {
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

zx_status_t NodeMgr::F2fsWriteNodePage(Page *page, WritebackControl *wbc) {
  SbInfo &sbi = fs_->GetSbInfo();
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
  WaitOnPageWriteback(page);

  /* get old block addr of this node page */
  nid = NidOfNode(page);
  nofs = OfsOfNode(page);
  ZX_ASSERT(page->index == nid);

  GetNodeInfo(nid, &ni);

  /* This page is already truncated */
  if (ni.blk_addr == kNullAddr) {
    return ZX_OK;
  }

  {
    fs::SharedLock rlock(sbi.fs_lock[static_cast<int>(LockType::kNodeOp)]);
    SetPageWriteback(page);

    /* insert node offset */
    fs_->Segmgr().WriteNodePage(page, nid, ni.blk_addr, &new_addr);
    SetNodeAddr(&ni, new_addr);
    DecPageCount(&sbi, CountType::kDirtyNodes);
  }

  // TODO: IMPL
  // unlock_page(page);
  return ZX_OK;
}

#if 0  // porting needed
int NodeMgr::F2fsWriteNodePages(struct address_space *mapping, WritebackControl *wbc) {
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
int NodeMgr::F2fsSetNodePageDirty(Page *page) {
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
void NodeMgr::F2fsInvalidateNodePage(Page *page, uint64_t offset) {
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

FreeNid *NodeMgr::LookupFreeNidList(nid_t n, list_node_t *head) {
  list_node_t *this_list;
  FreeNid *i = nullptr;
  list_for_every(head, this_list) {
    i = containerof(this_list, FreeNid, list);
    if (i->nid == n)
      break;
    i = nullptr;
  }
  return i;
}

void NodeMgr::DelFromFreeNidList(FreeNid *i) {
  list_delete(&i->list);
#if 0  // porting needed
  // kmem_cache_free(free_nid_slab, i);
#endif
  delete i;
}

int NodeMgr::AddFreeNid(NmInfo *nm_i, nid_t nid) {
  FreeNid *i;

  if (nm_i->fcnt > 2 * kMaxFreeNids)
    return 0;
retry:
#if 0  // porting needed (kmem_cache_alloc)
  // i = kmem_cache_alloc(free_nid_slab, GFP_NOFS);
#endif
  i = new FreeNid;
  if (!i) {
#if 0  // porting needed
    // cond_resched();
#endif
    goto retry;
  }
  i->nid = nid;
  i->state = static_cast<int>(NidState::kNidNew);

  std::lock_guard free_nid_lock(nm_i->free_nid_list_lock);
  if (LookupFreeNidList(nid, &nm_i->free_nid_list)) {
#if 0  // porting needed
    // kmem_cache_free(free_nid_slab, i);
#endif
    delete i;
    return 0;
  }
  list_add_tail(&nm_i->free_nid_list, &i->list);
  nm_i->fcnt++;
  return 1;
}

void NodeMgr::RemoveFreeNid(NmInfo *nm_i, nid_t nid) {
  FreeNid *i;
  std::lock_guard free_nid_lock(nm_i->free_nid_list_lock);
  i = LookupFreeNidList(nid, &nm_i->free_nid_list);
  if (i && i->state == static_cast<int>(NidState::kNidNew)) {
    DelFromFreeNidList(i);
    nm_i->fcnt--;
  }
}

int NodeMgr::ScanNatPage(NmInfo *nm_i, Page *nat_page, nid_t start_nid) {
  NatBlock *nat_blk = static_cast<NatBlock *>(PageAddress(nat_page));
  block_t blk_addr;
  int fcnt = 0;
  uint32_t i;

  /* 0 nid should not be used */
  if (start_nid == 0)
    ++start_nid;

  i = start_nid % kNatEntryPerBlock;

  for (; i < kNatEntryPerBlock; i++, start_nid++) {
    blk_addr = LeToCpu(nat_blk->entries[i].block_addr);
    ZX_ASSERT(blk_addr != kNewAddr);
    if (blk_addr == kNullAddr)
      fcnt += AddFreeNid(nm_i, start_nid);
  }
  return fcnt;
}

void NodeMgr::BuildFreeNids() {
  SbInfo &sbi = fs_->GetSbInfo();
  FreeNid *fnid, *next_fnid;
  NmInfo *nm_i = GetNmInfo(&sbi);
  CursegInfo *curseg = SegMgr::CURSEG_I(&sbi, CursegType::kCursegHotData);
  SummaryBlock *sum = curseg->sum_blk;
  nid_t nid = 0;
  bool is_cycled = false;
  uint64_t fcnt = 0;
  int i;

  nid = nm_i->next_scan_nid;
  nm_i->init_scan_nid = nid;

  RaNatPages(nid);

  while (true) {
    Page *page = GetCurrentNatPage(nid);

    fcnt += ScanNatPage(nm_i, page, nid);
    F2fsPutPage(page, 1);

    nid += (kNatEntryPerBlock - (nid % kNatEntryPerBlock));

    if (nid >= nm_i->max_nid) {
      nid = 0;
      is_cycled = true;
    }
    if (fcnt > kMaxFreeNids)
      break;
    if (is_cycled && nm_i->init_scan_nid <= nid)
      break;
  }

  nm_i->next_scan_nid = nid;

  {
    // find free nids from current sum_pages
    fbl::AutoLock curseg_lock(&curseg->curseg_mutex);
    for (i = 0; i < NatsInCursum(sum); i++) {
      block_t addr = LeToCpu(NatInJournal(sum, i).block_addr);
      nid = LeToCpu(NidInJournal(sum, i));
      if (addr == kNullAddr) {
        AddFreeNid(nm_i, nid);
      } else {
        RemoveFreeNid(nm_i, nid);
      }
    }
  }

  /* remove the free nids from current allocated nids */
  list_for_every_entry_safe (&nm_i->free_nid_list, fnid, next_fnid, FreeNid, list) {
    NatEntry *ne;

    fs::SharedLock lock(nm_i->nat_tree_lock);
    ne = LookupNatCache(nm_i, fnid->nid);
    if (ne && NatGetBlkaddr(ne) != kNullAddr)
      RemoveFreeNid(nm_i, fnid->nid);
  }
}

/*
 * If this function returns success, caller can obtain a new nid
 * from second parameter of this function.
 * The returned nid could be used ino as well as nid when inode is created.
 */
bool NodeMgr::AllocNid(nid_t *nid) {
  SbInfo &sbi = fs_->GetSbInfo();
  NmInfo *nm_i = GetNmInfo(&sbi);
  FreeNid *i = nullptr;
  list_node_t *this_list;
retry : {
  fbl::AutoLock lock(&nm_i->build_lock);
  if (!nm_i->fcnt) {
    /* scan NAT in order to build free nid list */
    BuildFreeNids();
    if (!nm_i->fcnt) {
      return false;
    }
  }
}

  /*
   * We check fcnt again since previous check is racy as
   * we didn't hold free_nid_list_lock. So other thread
   * could consume all of free nids.
   */
  if (!nm_i->fcnt) {
    goto retry;
  }

  std::lock_guard lock(nm_i->free_nid_list_lock);
  ZX_ASSERT(!list_is_empty(&nm_i->free_nid_list));

  list_for_every(&nm_i->free_nid_list, this_list) {
    i = containerof(this_list, FreeNid, list);
    if (i->state == static_cast<int>(NidState::kNidNew))
      break;
  }

  ZX_ASSERT(i->state == static_cast<int>(NidState::kNidNew));
  *nid = i->nid;
  i->state = static_cast<int>(NidState::kNidAlloc);
  nm_i->fcnt--;
  return true;
}

/**
 * alloc_nid() should be called prior to this function.
 */
void NodeMgr::AllocNidDone(nid_t nid) {
  SbInfo &sbi = fs_->GetSbInfo();
  NmInfo *nm_i = GetNmInfo(&sbi);
  FreeNid *i;

  std::lock_guard free_nid_lock(nm_i->free_nid_list_lock);
  i = LookupFreeNidList(nid, &nm_i->free_nid_list);
  if (i) {
    ZX_ASSERT(i->state == static_cast<int>(NidState::kNidAlloc));
    DelFromFreeNidList(i);
  }
}

/**
 * alloc_nid() should be called prior to this function.
 */
void NodeMgr::AllocNidFailed(nid_t nid) {
  SbInfo &sbi = fs_->GetSbInfo();
  AllocNidDone(nid);
  AddFreeNid(GetNmInfo(&sbi), nid);
}

void NodeMgr::RecoverNodePage(Page *page, Summary *sum, NodeInfo *ni, block_t new_blkaddr) {
  fs_->Segmgr().RewriteNodePage(page, sum, ni->blk_addr, new_blkaddr);
  SetNodeAddr(ni, new_blkaddr);
  ClearNodePageDirty(page);
}

zx_status_t NodeMgr::RecoverInodePage(Page *page) {
  SbInfo &sbi = fs_->GetSbInfo();
  //[[maybe_unused]] address_space *mapping = sbi.node_inode->i_mapping;
  Node *src, *dst;
  nid_t ino = InoOfNode(page);
  NodeInfo old_ni, new_ni;
  Page *ipage = nullptr;

  ipage = GrabCachePage(nullptr, NodeIno(&sbi), ino);
  if (!ipage)
    return ZX_ERR_NO_MEMORY;

  /* Should not use this inode  from free nid list */
  RemoveFreeNid(GetNmInfo(&sbi), ino);

  GetNodeInfo(ino, &old_ni);

#if 0  // porting needed
  // SetPageUptodate(ipage);
#endif
  FillNodeFooter(ipage, ino, ino, 0, true);

  src = static_cast<Node *>(PageAddress(page));
  dst = static_cast<Node *>(PageAddress(ipage));

  memcpy(dst, src, reinterpret_cast<uint64_t>(&src->i.i_ext) - reinterpret_cast<uint64_t>(&src->i));
  dst->i.i_size = 0;
  dst->i.i_blocks = 1;
  dst->i.i_links = 1;
  dst->i.i_xattr_nid = 0;

  new_ni = old_ni;
  new_ni.ino = ino;

  SetNodeAddr(&new_ni, kNewAddr);
  fs_->IncValidInodeCount();

  F2fsPutPage(ipage, 1);
  return ZX_OK;
}

zx_status_t NodeMgr::RestoreNodeSummary(F2fs *fs, uint32_t segno, SummaryBlock *sum) {
  SbInfo &sbi = fs->GetSbInfo();
  Node *rn;
  Summary *sum_entry;
  Page *page = nullptr;
  block_t addr;
  int i, last_offset;

  /* scan the node segment */
  last_offset = sbi.blocks_per_seg;
  addr = StartBlock(&sbi, segno);
  sum_entry = &sum->entries[0];

#if 0  // porting needed
  /* alloc temporal page for read node */
  // page = alloc_page(GFP_NOFS | __GFP_ZERO);
#endif
  page = GrabCachePage(nullptr, NodeIno(&sbi), addr);
  if (page == nullptr)
    return ZX_ERR_NO_MEMORY;
#if 0  // porting needed
  // lock_page(page);
#endif

  for (i = 0; i < last_offset; i++, sum_entry++) {
    if (VnodeF2fs::Readpage(fs, page, addr, kReadSync))
      goto out;

    rn = static_cast<Node *>(PageAddress(page));
    sum_entry->nid = rn->footer.nid;
    sum_entry->version = 0;
    sum_entry->ofs_in_node = 0;
    addr++;

    /*
     * In order to read next node page,
     * we must clear PageUptodate flag.
     */
#if 0  // porting needed
    // ClearPageUptodate(page);
#endif
  }
out:
#if 0  // porting needed
  // unlock_page(page);
  //__free_pages(page, 0);
#endif
  F2fsPutPage(page, 1);
  return ZX_OK;
}

bool NodeMgr::FlushNatsInJournal() {
  SbInfo &sbi = fs_->GetSbInfo();
  NmInfo *nm_i = GetNmInfo(&sbi);
  CursegInfo *curseg = fs_->Segmgr().CURSEG_I(&sbi, CursegType::kCursegHotData);
  SummaryBlock *sum = curseg->sum_blk;
  int i;

  fbl::AutoLock curseg_lock(&curseg->curseg_mutex);

  {
    fs::SharedLock nat_lock(nm_i->nat_tree_lock);
    size_t dirty_nat_cnt = list_length(&nm_i->dirty_nat_entries);
    if ((NatsInCursum(sum) + dirty_nat_cnt) <= kNatJournalEntries) {
      return false;
    }
  }

  for (i = 0; i < NatsInCursum(sum); i++) {
    NatEntry *ne = nullptr;
    RawNatEntry raw_ne = NatInJournal(sum, i);
    nid_t nid = LeToCpu(NidInJournal(sum, i));

    while (!ne) {
      std::lock_guard nat_lock(nm_i->nat_tree_lock);
      ne = LookupNatCache(nm_i, nid);
      if (ne) {
        SetNatCacheDirty(nm_i, ne);
      } else {
        ne = GrabNatEntry(nm_i, nid);
        if (!ne) {
          continue;
        }
        NatSetBlkaddr(ne, LeToCpu(raw_ne.block_addr));
        NatSetIno(ne, LeToCpu(raw_ne.ino));
        NatSetVersion(ne, raw_ne.version);
        SetNatCacheDirty(nm_i, ne);
      }
    }
  }
  UpdateNatsInCursum(sum, -i);
  return true;
}

// This function is called during the checkpointing process.
void NodeMgr::FlushNatEntries() {
  SbInfo &sbi = fs_->GetSbInfo();
  NmInfo *nm_i = GetNmInfo(&sbi);
  CursegInfo *curseg = fs_->Segmgr().CURSEG_I(&sbi, CursegType::kCursegHotData);
  SummaryBlock *sum = curseg->sum_blk;
  list_node_t *cur, *n;
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
  list_for_every_safe(&nm_i->dirty_nat_entries, cur, n) {
    NatEntry *ne;
    nid_t nid;
    RawNatEntry raw_ne;
    int offset = -1;
    __UNUSED block_t old_blkaddr, new_blkaddr;

    ne = containerof(cur, NatEntry, list);
    nid = NatGetNid(ne);

    if (NatGetBlkaddr(ne) == kNewAddr)
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

    new_blkaddr = NatGetBlkaddr(ne);

    raw_ne.ino = CpuToLe(NatGetIno(ne));
    raw_ne.block_addr = CpuToLe(new_blkaddr);
    raw_ne.version = NatGetVersion(ne);

    if (offset < 0) {
      nat_blk->entries[nid - start_nid] = raw_ne;
    } else {
      SetNatInJournal(sum, offset, raw_ne);
      SetNidInJournal(sum, offset, CpuToLe(nid));
    }

    if (NatGetBlkaddr(ne) == kNullAddr) {
      {
        std::lock_guard nat_lock(nm_i->nat_tree_lock);
        DelFromNatCache(nm_i, ne);
      }

      // We can reuse this freed nid at this point
      AddFreeNid(GetNmInfo(&sbi), nid);
    } else {
      std::lock_guard nat_lock(nm_i->nat_tree_lock);
      ClearNatCacheDirty(nm_i, ne);
      ne->checkpointed = true;
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
  TryToFreeNats(nm_i->nat_cnt - kNmWoutThreshold);
}

zx_status_t NodeMgr::InitNodeManager() {
  SbInfo &sbi = fs_->GetSbInfo();
  const SuperBlock *sb_raw = RawSuper(&sbi);
  NmInfo *nm_i = GetNmInfo(&sbi);
  uint8_t *version_bitmap;
  uint32_t nat_segs, nat_blocks;

  nm_i->nat_blkaddr = LeToCpu(sb_raw->nat_blkaddr);
  /* segment_count_nat includes pair segment so divide to 2. */
  nat_segs = LeToCpu(sb_raw->segment_count_nat) >> 1;
  nat_blocks = nat_segs << LeToCpu(sb_raw->log_blocks_per_seg);
  nm_i->max_nid = kNatEntryPerBlock * nat_blocks;
  nm_i->fcnt = 0;
  nm_i->nat_cnt = 0;

  list_initialize(&nm_i->free_nid_list);
#if 0  // porting needed (belows are of no use currently)
  // INIT_RADIX_TREE(&nm_i->nat_root, GFP_ATOMIC);
#endif
  list_initialize(&nm_i->nat_entries);
  list_initialize(&nm_i->dirty_nat_entries);

  nm_i->bitmap_size = BitmapSize(&sbi, MetaBitmap::kNatBitmap);
  nm_i->init_scan_nid = LeToCpu(sbi.ckpt->next_free_nid);
  nm_i->next_scan_nid = LeToCpu(sbi.ckpt->next_free_nid);

  nm_i->nat_bitmap = static_cast<char *>(malloc(nm_i->bitmap_size));
  memset(nm_i->nat_bitmap, 0, nm_i->bitmap_size);
  nm_i->nat_prev_bitmap = static_cast<char *>(malloc(nm_i->bitmap_size));
  memset(nm_i->nat_prev_bitmap, 0, nm_i->bitmap_size);

  if (!nm_i->nat_bitmap)
    return ZX_ERR_NO_MEMORY;
  version_bitmap = static_cast<uint8_t *>(BitmapPrt(&sbi, MetaBitmap::kNatBitmap));
  if (!version_bitmap)
    return ZX_ERR_INVALID_ARGS;

  /* copy version bitmap */
  memcpy(nm_i->nat_bitmap, version_bitmap, nm_i->bitmap_size);
  memcpy(nm_i->nat_prev_bitmap, nm_i->nat_bitmap, nm_i->bitmap_size);
  return ZX_OK;
}

zx_status_t NodeMgr::BuildNodeManager() {
  SbInfo &sbi = fs_->GetSbInfo();
  int err;

  sbi.nm_info = new NmInfo;
  if (!sbi.nm_info)
    return ZX_ERR_NO_MEMORY;

  err = InitNodeManager();
  if (err)
    return err;

  BuildFreeNids();
  return ZX_OK;
}

void NodeMgr::DestroyNodeManager() {
  SbInfo &sbi = fs_->GetSbInfo();
  NmInfo *nm_i = GetNmInfo(&sbi);
  FreeNid *i, *next_i;
  NatEntry *natvec[kNatvecSize];
  nid_t nid = 0;
  uint32_t found;

  if (!nm_i)
    return;

  // destroy free nid list
  std::lock_guard free_nid_lock(nm_i->free_nid_list_lock);
  list_for_every_entry_safe (&nm_i->free_nid_list, i, next_i, FreeNid, list) {
    ZX_ASSERT(i->state != static_cast<int>(NidState::kNidAlloc));
    DelFromFreeNidList(i);
    nm_i->fcnt--;
  }
  ZX_ASSERT(!nm_i->fcnt);

  /* destroy nat cache */
  std::lock_guard nat_lock(nm_i->nat_tree_lock);
  while ((found = GangLookupNatCache(nm_i, nid, kNatvecSize, natvec))) {
    uint32_t idx;
    for (idx = 0; idx < found; idx++) {
      NatEntry *e = natvec[idx];
      nid = NatGetNid(e) + 1;
      DelFromNatCache(nm_i, e);
    }
  }
  // TODO: Check nm_i->nat_cnt
  // ZX_ASSERT(!nm_i->nat_cnt);

  delete[] nm_i->nat_bitmap;
  delete[] nm_i->nat_prev_bitmap;
  sbi.nm_info = nullptr;
  delete nm_i;
}

zx_status_t NodeMgr::CreateNodeManagerCaches() {
#if 0  // porting needed
  // NatEntry_slab = KmemCacheCreate("NatEntry",
  //                 sizeof(NatEntry), NULL);
  // if (!NatEntry_slab)
  //         return -ENOMEM;

  // free_nid_slab = KmemCacheCreate("free_nid",
  //                 sizeof(FreeNid), NULL);
  // if (!free_nid_slab) {
  //         kmem_cache_destroy(NatEntry_slab);
  //         return -ENOMEM;
  // }
#endif
  return ZX_OK;
}

void NodeMgr::DestroyNodeManagerCaches() {
#if 0  // porting needed
  // kmem_cache_destroy(free_nid_slab);
  // kmem_cache_destroy(NatEntry_slab);
#endif
}

block_t NodeMgr::StartBidxOfNode(Page *node_page) {
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
