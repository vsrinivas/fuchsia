// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/f2fs/f2fs.h"

namespace f2fs {

// We guarantee no failure on the returned page.
Page *F2fs::GrabMetaPage(pgoff_t index) {
  Page *page = nullptr;

  while (!(page = GrabCachePage(nullptr, superblock_info_->GetMetaIno(), index))) {
#if 0  // porting needed
    // cond_resched();
#endif
  }
  /* We wait writeback only inside grab_meta_page() */
  WaitOnPageWriteback(page);
  SetPageUptodate(page);
  return page;
}

Page *F2fs::GetMetaPage(pgoff_t index) {
  Page *page = nullptr;
repeat:
  page = GrabCachePage(nullptr, superblock_info_->GetMetaIno(), index);
  if (!page) {
#if 0  // porting needed
    // cond_resched();
#endif
    goto repeat;
  }
  if (VnodeF2fs::Readpage(this, static_cast<Page *>(PageAddress(page)), static_cast<block_t>(index),
                          kReadSync)) {
    F2fsPutPage(page, 1);
    goto repeat;
  }
#if 0  // porting needed
  // mark_page_accessed(page);
#endif

  /* We do not allow returning an errorneous page */
  return page;
}

zx_status_t F2fs::F2fsWriteMetaPage(Page *page, WritebackControl *wbc) {
  zx_status_t err;

  WaitOnPageWriteback(page);

  err = this->GetSegmentManager().WriteMetaPage(page, wbc);
  if (err) {
#if 0  // porting needed
    // wbc->pages_skipped++;
    // set_page_dirty(page, this);
#else
    FlushDirtyMetaPage(this, page);
#endif
  }

  GetSuperblockInfo().DecPageCount(CountType::kDirtyMeta);

  /* In this case, we should not unlock this page */
#if 0  // porting needed
  // if (err != kAopWritepageActivate)
  // unlock_page(page);
#endif
  return err;
}

#if 0  // porting needed
// int F2fs::F2fsWriteMetaPages(address_space *mapping, WritebackControl *wbc) {
//   struct block_device *bdev = superblock_info_->sb->s_bdev;
//   long written;

//   if (wbc->for_kupdate)
//   	return 0;

//   if (superblock_info_->GetPages(CountType::kDirtyMeta) == 0)
//   	return 0;

//   /* if mounting is failed, skip writing node pages */
//   mtx_lock(&superblock_info_->GetCheckpointMutex());
//   written = sync_meta_pages(superblock_info_.get(), META, bio_get_nr_vecs(bdev));
//   mtx_unlock(&superblock_info_->GetCheckpointMutex());
//   wbc->nr_to_write -= written;
//   return 0;
// }
#endif

int64_t F2fs::SyncMetaPages(PageType type, int64_t nr_to_write) {
#if 0  // porting needed
  // address_space *mapping = superblock_info->meta_inode->i_mapping;
  // pgoff_t index = 0, end = LONG_MAX;
  // pagevec pvec;
  // int64_t nwritten = 0;
  // struct WritebackControl wbc = {
  // 	.for_reclaim = 0,
  // };

  // pagevec_init(&pvec, 0);

  // while (index <= end) {
  // 	int i, nr_pages;
  // 	nr_pages = pagevec_lookup_tag(&pvec, mapping, &index,
  // 			PAGECACHE_TAG_DIRTY,
  // 			min(end - index, (pgoff_t)PAGEVEC_SIZE-1) + 1);
  // 	if (nr_pages == 0)
  // 		break;

  // 	for (i = 0; i < nr_pages; i++) {
  // 		page *page = pvec.pages[i];
  // 		lock_page(page);
  // 		BUG_ON(page->mapping != mapping);
  // 		BUG_ON(!PageDirty(page));
  // 		ClearPageDirtyForIo(page);
  // 		f2fs_write_meta_page(page, &wbc);
  // 		if (nwritten++ >= nr_to_write)
  // 			break;
  // 	}
  // 	pagevec_release(&pvec);
  // 	cond_resched();
  // }

  // if (nwritten)
  // 	f2fs_submit_bio(superblock_info, type, nr_to_write == LONG_MAX);

  // return nwritten;
#else
  return 0;
#endif
}

#if 0  // porting needed
// int F2fs::F2fsSetMetaPageDirty(Page *page) {
//   SetPageUptodate(page);
//   if (!PageDirty(page)) {
//     // __set_page_dirty_nobuffers(page);
//     FlushDirtyMetaPage(this, page);
//     GetSuperblockInfo().IncPageCount(CountType::kDirtyMeta);
//     GetSuperblockInfo().SetDirty();
//     return 1;
//   }
//   return 0;
// }
#endif

zx_status_t F2fs::CheckOrphanSpace() {
  SuperblockInfo &superblock_info = GetSuperblockInfo();
  uint32_t max_orphans;
  zx_status_t err = 0;

  /*
   * considering 512 blocks in a segment 5 blocks are needed for cp
   * and log segment summaries. Remaining blocks are used to keep
   * orphan entries with the limitation one reserved segment
   * for cp pack we can have max 1020*507 orphan entries
   */
  max_orphans = (superblock_info.GetBlocksPerSeg() - 5) * kOrphansPerBlock;
  std::lock_guard lock(superblock_info.GetOrphanInodeMutex());
  if (superblock_info.GetOrphanCount() >= max_orphans)
    err = ZX_ERR_NO_SPACE;
  return err;
}

void F2fs::AddOrphanInode(VnodeF2fs *vnode) {
  AddOrphanInode(vnode->GetKey());
  if (vnode->IsDir()) {
    vnode->Notify(vnode->GetName(), fuchsia_io::wire::kWatchEventDeleted);
  }
  if (vnode->ClearDirty()) {
    ZX_ASSERT(GetVCache().RemoveDirty(vnode) == ZX_OK);
  }
}

void F2fs::AddOrphanInode(nid_t ino) {
  SuperblockInfo &superblock_info = GetSuperblockInfo();
  OrphanInodeEntry *new_entry = nullptr, *orphan = nullptr;

  std::lock_guard lock(superblock_info.GetOrphanInodeMutex());
  list_node_t *head = &superblock_info.GetOrphanInodeList(), *this_node;
  list_for_every(head, this_node) {
    orphan = containerof(this_node, OrphanInodeEntry, list);
    if (orphan->ino == ino)
      return;
    if (orphan->ino > ino)
      break;
    orphan = nullptr;
  }

  // TODO: handle a failing case
  new_entry = new OrphanInodeEntry;
  ZX_ASSERT(new_entry != nullptr);

  new_entry->ino = ino;
  list_initialize(&new_entry->list);

  // add new_entry into list which is sorted by inode number
  if (orphan) {
    OrphanInodeEntry *prev;

    // get previous entry
    prev = containerof(orphan->list.prev, OrphanInodeEntry, list);
    if (&prev->list != head) {
      // insert new orphan inode entry
      list_add(&prev->list, &new_entry->list);
    } else {
      list_add(head, &new_entry->list);
    }
  } else {
    list_add_tail(head, &new_entry->list);
  }
  superblock_info.IncNrOrphans();
}

void F2fs::RemoveOrphanInode(nid_t ino) {
  SuperblockInfo &superblock_info = GetSuperblockInfo();
  list_node_t *this_node, *next, *head;
  OrphanInodeEntry *orphan;

  std::lock_guard lock(superblock_info.GetOrphanInodeMutex());
  head = &superblock_info.GetOrphanInodeList();
  list_for_every_safe(head, this_node, next) {
    orphan = containerof(this_node, OrphanInodeEntry, list);
    if (orphan->ino == ino) {
      list_delete(&orphan->list);
#if 0  // porting needed
      // kmem_cache_free(orphan_entry_slab, orphan);
#endif
      delete orphan;
      superblock_info.DecNrOrphans();
      break;
    }
  }
}

void F2fs::RecoverOrphanInode(nid_t ino) {
  fbl::RefPtr<VnodeF2fs> vnode;
  zx_status_t ret;
  ret = VnodeF2fs::Vget(this, ino, &vnode);
  ZX_ASSERT(ret == ZX_OK);
  vnode->ClearNlink();

  // truncate all the data and nodes in VnodeF2fs::Recycle()
  // Iput(vnode.get());
  vnode.reset();
}

zx_status_t F2fs::RecoverOrphanInodes() {
  SuperblockInfo &superblock_info = GetSuperblockInfo();
  block_t start_blk, orphan_blkaddr, i, j;

  if (!(superblock_info.GetCheckpoint().ckpt_flags & kCpOrphanPresentFlag))
    return ZX_OK;
  superblock_info.SetOnRecovery();
  start_blk = superblock_info.StartCpAddr() + 1;
  orphan_blkaddr = superblock_info.StartSumAddr() - 1;

  for (i = 0; i < orphan_blkaddr; i++) {
    Page *page = GetMetaPage(start_blk + i);
    OrphanBlock *orphan_blk;

    orphan_blk = static_cast<OrphanBlock *>(PageAddress(page));
    uint32_t entry_count = LeToCpu(orphan_blk->entry_count);
    // TODO: Need to set NeedChkp flag to repair the fs when fsck repair is available.
    // For now, we trigger assertion.
    ZX_ASSERT(entry_count <= kOrphansPerBlock);
    for (j = 0; j < entry_count; j++) {
      nid_t ino = LeToCpu(orphan_blk->ino[j]);
      RecoverOrphanInode(ino);
    }
    F2fsPutPage(page, 1);
  }
  // clear Orphan Flag
  superblock_info.GetCheckpoint().ckpt_flags &= (~kCpOrphanPresentFlag);
  superblock_info.ClearOnRecovery();
  return ZX_OK;
}

void F2fs::WriteOrphanInodes(block_t start_blk) {
  SuperblockInfo &superblock_info = GetSuperblockInfo();
  list_node_t *head, *this_node, *next;
  OrphanBlock *orphan_blk = nullptr;
  Page *page = nullptr;
  uint32_t nentries = 0;
  uint16_t index = 1;
  uint16_t orphan_blocks;

  orphan_blocks = static_cast<uint16_t>(
      (superblock_info.GetOrphanCount() + (kOrphansPerBlock - 1)) / kOrphansPerBlock);

  std::lock_guard lock(superblock_info.GetOrphanInodeMutex());
  head = &superblock_info.GetOrphanInodeList();

  /* loop for each orphan inode entry and write them in Jornal block */
  list_for_every_safe(head, this_node, next) {
    OrphanInodeEntry *orphan;

    orphan = containerof(this_node, OrphanInodeEntry, list);

    if (nentries == kOrphansPerBlock) {
      /*
       * an orphan block is full of 1020 entries,
       * then we need to flush current orphan blocks
       * and bring another one in memory
       */
      orphan_blk->blk_addr = CpuToLe(index);
      orphan_blk->blk_count = CpuToLe(orphan_blocks);
      orphan_blk->entry_count = CpuToLe(nentries);
#if 0  // porting needed
      // set_page_dirty(page, this);
#else
      FlushDirtyMetaPage(this, page);
#endif
      F2fsPutPage(page, 1);
      index++;
      start_blk++;
      nentries = 0;
      page = nullptr;
    }
    if (!page) {
      page = GrabMetaPage(start_blk);
      orphan_blk = static_cast<OrphanBlock *>(PageAddress(page));
      memset(orphan_blk, 0, sizeof(*orphan_blk));
    }
    orphan_blk->ino[nentries++] = CpuToLe(orphan->ino);
  }
  if (page) {
    orphan_blk->blk_addr = CpuToLe(index);
    orphan_blk->blk_count = CpuToLe(orphan_blocks);
    orphan_blk->entry_count = CpuToLe(nentries);
#if 0  // porting needed
  // set_page_dirty(page, this);
#else
    FlushDirtyMetaPage(this, page);
#endif
    F2fsPutPage(page, 1);
  }
}

Page *F2fs::ValidateCheckpoint(block_t cp_addr, uint64_t *version) {
  Page *cp_page_1 = nullptr, *cp_page_2 = nullptr;
  uint64_t blk_size = superblock_info_->GetBlocksize();
  Checkpoint *cp_block;
  uint64_t cur_version = 0, pre_version = 0;
  uint32_t crc = 0;
  size_t crc_offset;

  /* Read the 1st cp block in this CP pack */
  cp_page_1 = GetMetaPage(cp_addr);

  /* get the version number */
  cp_block = static_cast<Checkpoint *>(PageAddress(cp_page_1));
  crc_offset = LeToCpu(cp_block->checksum_offset);
  if (crc_offset >= blk_size)
    goto invalid_cp1;

  crc = *reinterpret_cast<uint32_t *>(reinterpret_cast<uint8_t *>(cp_block) + crc_offset);
  if (!F2fsCrcValid(crc, cp_block, static_cast<uint32_t>(crc_offset)))
    goto invalid_cp1;

  pre_version = LeToCpu(cp_block->checkpoint_ver);

  /* Read the 2nd cp block in this CP pack */
  cp_addr += LeToCpu(cp_block->cp_pack_total_block_count) - 1;
  cp_page_2 = GetMetaPage(cp_addr);

  cp_block = static_cast<Checkpoint *>(PageAddress(cp_page_2));
  crc_offset = LeToCpu(cp_block->checksum_offset);
  if (crc_offset >= blk_size)
    goto invalid_cp2;

  crc = *reinterpret_cast<uint32_t *>(reinterpret_cast<uint8_t *>(cp_block) + crc_offset);
  if (!F2fsCrcValid(crc, cp_block, static_cast<uint32_t>(crc_offset)))
    goto invalid_cp2;

  cur_version = LeToCpu(cp_block->checkpoint_ver);

  if (cur_version == pre_version) {
    *version = cur_version;
    F2fsPutPage(cp_page_2, 1);
    return cp_page_1;
  }
invalid_cp2:
  F2fsPutPage(cp_page_2, 1);
invalid_cp1:
  F2fsPutPage(cp_page_1, 1);
  return nullptr;
}

zx_status_t F2fs::GetValidCheckpoint() {
  Checkpoint *cp_block;
  SuperBlock &fsb = RawSb();
  Page *cp1 = nullptr, *cp2 = nullptr, *cur_page = nullptr;
  uint64_t blk_size = superblock_info_->GetBlocksize();
  uint64_t cp1_version = 0, cp2_version = 0;
  block_t cp_start_blk_no;

  /*
   * Finding out valid cp block involves read both
   * sets( cp pack1 and cp pack 2)
   */
  cp_start_blk_no = LeToCpu(fsb.cp_blkaddr);
  cp1 = ValidateCheckpoint(cp_start_blk_no, &cp1_version);

  /* The second checkpoint pack should start at the next segment */
  cp_start_blk_no += 1 << LeToCpu(fsb.log_blocks_per_seg);
  cp2 = ValidateCheckpoint(cp_start_blk_no, &cp2_version);

  if (cp1 && cp2) {
    if (VerAfter(cp2_version, cp1_version)) {
      cur_page = cp2;
    } else {
      cur_page = cp1;
    }
  } else if (cp1) {
    cur_page = cp1;
  } else if (cp2) {
    cur_page = cp2;
  } else {
    goto fail_no_cp;
  }

  cp_block = static_cast<Checkpoint *>(PageAddress(cur_page));
  memcpy(&superblock_info_->GetCheckpoint(), cp_block, blk_size);

#ifdef F2FS_BU_DEBUG
  FX_LOGS(DEBUG) << "F2fs::GetValidCheckpoint" << std::endl;
  for (int i = 0; i < kMaxActiveNodeLogs; i++) {
    FX_LOGS(DEBUG) << "[" << i << "] cur_node_segno " << cp_block->cur_node_segno[i]
                   << ", cur_node_blkoff=" << cp_block->cur_node_blkoff[i];
  }

  for (int i = 0; i < kMaxActiveNodeLogs; i++) {
    FX_LOGS(DEBUG) << "[" << i << "] cur_data_segno " << cp_block->cur_data_segno[i]
                   << ", cur_data_blkoff=" << cp_block->cur_data_blkoff[i];
  }
#endif

  F2fsPutPage(cp1, 1);
  F2fsPutPage(cp2, 1);
  return 0;

fail_no_cp:
  return ZX_ERR_INVALID_ARGS;
}

#if 0  // porting needed
// void F2fs::SetDirtyDirPage(VnodeF2fs *vnode, Page *page) {
//   SuperblockInfo &superblock_info = GetSuperblockInfo();
//   list_node_t *head = &superblock_info.dir_inode_list;
//   DirInodeEntry *new_entry;
//   list_node_t *this_node;

//   if (!vnode->IsDir())
//     return;
// retry:
//   // new = kmem_cache_alloc(inode_entry_slab, GFP_NOFS);
//   new_entry = new DirInodeEntry;
//   if (!new_entry) {
//     // cond_resched();
//     goto retry;
//   }
//   new_entry->vnode = vnode;
//   list_initialize(&new_entry->list);

//   SpinLock(&superblock_info.GetDirInodeLock());
//   list_for_every(head, this_node) {
//     DirInodeEntry *entry;
//     entry = containerof(this_node, DirInodeEntry, list);
//     if (entry->vnode == vnode) {
//       // kmem_cache_free(inode_entry_slab, new_entry);
//       delete new_entry;
//       goto out;
//     }
//   }
//   list_add_tail(&new_entry->list, head);
//   superblock_info.n_dirty_dirs++;

//   BUG_ON(!S_ISDIR(inode->i_mode));
// out:
//   superblock_info.IncPageCount(CountType::kDirtyDents);
//   InodeIncDirtyDents(vnode);
//   // SetPagePrivate(page);

//   SpinUnlock(&superblock_info.GetDirInodeLock());
// }

// void F2fs::RemoveDirtyDirInode(VnodeF2fs *vnode) {
//   SuperblockInfo &superblock_info = GetSuperblockInfo();
//   list_node_t *head = &superblock_info.dir_inode_list;
//   list_node_t *this_node;

//   if (!vnode->IsDir())
//     return;

//   SpinLock(&superblock_info.GetDirInodeLock());
//   // if (AtomicRead(&F2FS_I(vnode)->dirty_dents))
//   if (vnode->fi_.dirty_dents)
//     goto out;

//   list_for_every(head, this_node) {
//     DirInodeEntry *entry;
//     entry = containerof(this_node, DirInodeEntry, list);
//     if (entry->vnode == vnode) {
//       list_delete(&entry->list);
//       // kmem_cache_free(inode_entry_slab, entry);
//       delete entry;
//       superblock_info.n_dirty_dirs--;
//       break;
//     }
//   }
// out:
//   SpinUnlock(&superblock_info.GetDirInodeLock());
// }
#endif

void F2fs::SyncDirtyDirInodes() {
  // Handle unlinked vnodes
  GetVCache().ForDirtyVnodesIf(
      [this](fbl::RefPtr<VnodeF2fs> &vnode) {
        if (!vnode->ShouldFlush()) {
          GetVCache().RemoveDirty(vnode.get());
          return ZX_OK;
        }
        return ZX_ERR_NEXT;
      },
      [](fbl::RefPtr<VnodeF2fs> &vnode) {
        if (!vnode->ShouldFlush()) {
          return ZX_OK;
        }
        return ZX_ERR_NEXT;
      });

  // TODO: Do flush dirty entry blocks when pager is available.
#if 0  // porting needed
  SuperblockInfo &superblock_info = GetSuperblockInfo();
  list_node_t *head = &superblock_info.GetDirInodeList();
  DirInodeEntry *entry;
  fbl::RefPtr<VnodeF2fs> vnode;

  while (true) {
		fbl::AutoLock lock(&superblock_info.GetDirInodeLock());
    if (list_is_empty(head)) {
      break;
    }
    entry = containerof(head->next, DirInodeEntry, list);
    vnode.reset(static_cast<VnodeF2fs *>(Igrab(entry->vnode)));
    SpinUnlock(&superblock_info.GetDirInodeLock());
    if (vnode) {
    // filemap_flush(vnode->i_mapping);
      Iput(vnode.get());
      vnode.reset();
    } else {
      /*
       * We should submit bio, since it exists several
       * wribacking dentry pages in the freeing inode.
       */
      // TODO(unknown): bio[type] is empty
      // GetSegmentManager().SubmitBio(DATA, true);
    }
  }
#endif
}

/**
 * Freeze all the FS-operations for checkpoint.
 */
void F2fs::BlockOperations() TA_NO_THREAD_SAFETY_ANALYSIS {
  SuperblockInfo &superblock_info = GetSuperblockInfo();
  struct WritebackControl wbc = {
#if 0  // porting needed
      // .nr_to_write = LONG_MAX,
      // .sync_mode = WB_SYNC_ALL,

      // .for_reclaim = 0,
#endif
  };

retry_dents:
  // write all the dirty dentry pages
  SyncDirtyDirInodes();

  // Stop file operation
  superblock_info.mutex_lock_op(LockType::kFileOp);
  if (superblock_info.GetPages(CountType::kDirtyDents)) {
    superblock_info.mutex_unlock_op(LockType::kFileOp);
    goto retry_dents;
  }

  // POR: we should ensure that there is no dirty node pages
  // until finishing nat/sit flush.
retry:
  GetNodeManager().SyncNodePages(0, &wbc);

  superblock_info.mutex_lock_op(LockType::kNodeOp);

  if (superblock_info.GetPages(CountType::kDirtyNodes)) {
    superblock_info.mutex_unlock_op(LockType::kNodeOp);
    goto retry;
  }
}

void F2fs::UnblockOperations() TA_NO_THREAD_SAFETY_ANALYSIS {
  SuperblockInfo &superblock_info = GetSuperblockInfo();
  superblock_info.mutex_unlock_op(LockType::kNodeOp);
  superblock_info.mutex_unlock_op(LockType::kFileOp);
}

void F2fs::DoCheckpoint(bool is_umount) {
  SuperblockInfo &superblock_info = GetSuperblockInfo();
  Checkpoint &ckpt = superblock_info.GetCheckpoint();
  nid_t last_nid = 0;
  block_t start_blk;
  Page *cp_page = nullptr;
  uint32_t data_sum_blocks, orphan_blocks;
  void *kaddr;
  uint32_t crc32 = 0;
  int i;

  /* Flush all the NAT/SIT pages */
  while (superblock_info.GetPages(CountType::kDirtyMeta))
    SyncMetaPages(PageType::kMeta, LONG_MAX);

  GetNodeManager().NextFreeNid(&last_nid);

  /*
   * modify checkpoint
   * version number is already updated
   */
  ckpt.elapsed_time = CpuToLe(static_cast<uint64_t>(GetSegmentManager().GetMtime()));
  ckpt.valid_block_count = CpuToLe(ValidUserBlocks());
  ckpt.free_segment_count = CpuToLe(GetSegmentManager().FreeSegments());
  for (i = 0; i < 3; i++) {
    ckpt.cur_node_segno[i] =
        CpuToLe(GetSegmentManager().CursegSegno(i + static_cast<int>(CursegType::kCursegHotNode)));
    ckpt.cur_node_blkoff[i] =
        CpuToLe(GetSegmentManager().CursegBlkoff(i + static_cast<int>(CursegType::kCursegHotNode)));
    ckpt.alloc_type[i + static_cast<int>(CursegType::kCursegHotNode)] =
        GetSegmentManager().CursegAllocType(i + static_cast<int>(CursegType::kCursegHotNode));
  }
  for (i = 0; i < 3; i++) {
    ckpt.cur_data_segno[i] =
        CpuToLe(GetSegmentManager().CursegSegno(i + static_cast<int>(CursegType::kCursegHotData)));
    ckpt.cur_data_blkoff[i] =
        CpuToLe(GetSegmentManager().CursegBlkoff(i + static_cast<int>(CursegType::kCursegHotData)));
    ckpt.alloc_type[i + static_cast<int>(CursegType::kCursegHotData)] =
        GetSegmentManager().CursegAllocType(i + static_cast<int>(CursegType::kCursegHotData));

#ifdef F2FS_BU_DEBUG
    FX_LOGS(DEBUG) << std::endl << "F2fs::DoCheckpoint ";
    FX_LOGS(DEBUG) << "[" << i << "] cur_data_segno " << ckpt.cur_data_segno[i]
                   << ", cur_data_blkoff=" << ckpt.cur_data_blkoff[i];

    FX_LOGS(DEBUG) << "[" << i << "] cur_node_segno " << ckpt.cur_node_segno[i]
                   << ", cur_node_blkoff=" << ckpt.cur_node_blkoff[i];
#endif
  }

  ckpt.valid_node_count = CpuToLe(ValidNodeCount());
  ckpt.valid_inode_count = CpuToLe(ValidInodeCount());
  ckpt.next_free_nid = CpuToLe(last_nid);

  /* 2 cp  + n data seg summary + orphan inode blocks */
  data_sum_blocks = GetSegmentManager().NpagesForSummaryFlush();
  if (data_sum_blocks < 3) {
    ckpt.ckpt_flags |= kCpCompactSumFlag;
  } else {
    ckpt.ckpt_flags &= (~kCpCompactSumFlag);
  }

  orphan_blocks = static_cast<uint32_t>((superblock_info.GetOrphanCount() + kOrphansPerBlock - 1) /
                                        kOrphansPerBlock);
  ckpt.cp_pack_start_sum = 1 + orphan_blocks;
  ckpt.cp_pack_total_block_count = 2 + data_sum_blocks + orphan_blocks;

  if (is_umount) {
    ckpt.ckpt_flags |= kCpUmountFlag;
    ckpt.cp_pack_total_block_count += kNrCursegNodeType;
  } else {
    ckpt.ckpt_flags &= (~kCpUmountFlag);
  }

  if (superblock_info.GetOrphanCount() > 0) {
    ckpt.ckpt_flags |= kCpOrphanPresentFlag;
  } else {
    ckpt.ckpt_flags &= (~kCpOrphanPresentFlag);
  }

  /* update SIT/NAT bitmap */
  GetSegmentManager().GetSitBitmap(superblock_info.BitmapPtr(MetaBitmap::kSitBitmap));
  GetNodeManager().GetNatBitmap(superblock_info.BitmapPtr(MetaBitmap::kNatBitmap));

  crc32 = CpuToLe(F2fsCrc32(&ckpt, LeToCpu(ckpt.checksum_offset)));
  memcpy(reinterpret_cast<uint8_t *>(&ckpt) + LeToCpu(ckpt.checksum_offset), &crc32,
         sizeof(uint32_t));

  start_blk = superblock_info.StartCpAddr();

  /* write out checkpoint buffer at block 0 */
  cp_page = GrabMetaPage(start_blk++);
  kaddr = PageAddress(cp_page);
  memcpy(kaddr, &ckpt, (1 << superblock_info.GetLogBlocksize()));
#if 0  // porting needed
  // set_page_dirty(cp_page, this);
#else
  FlushDirtyMetaPage(this, cp_page);
#endif
  F2fsPutPage(cp_page, 1);

  if (superblock_info.GetOrphanCount() > 0) {
    WriteOrphanInodes(start_blk);
    start_blk += orphan_blocks;
  }

  GetSegmentManager().WriteDataSummaries(start_blk);
  start_blk += data_sum_blocks;
  if (is_umount) {
    GetSegmentManager().WriteNodeSummaries(start_blk);
    start_blk += kNrCursegNodeType;
  }

  /* writeout checkpoint block */
  cp_page = GrabMetaPage(start_blk);
  kaddr = PageAddress(cp_page);
  memcpy(kaddr, &ckpt, (1 << superblock_info.GetLogBlocksize()));
#if 0  // porting needed
  // set_page_dirty(cp_page, this);
#else
  FlushDirtyMetaPage(this, cp_page);
#endif
  F2fsPutPage(cp_page, 1);

  /* wait for previous submitted node/meta pages writeback */
#if 0  // porting needed
  // while (superblock_info.GetPages(kWriteback))
  //	congestion_wait(BLK_RW_ASYNC, HZ / 50);

  // filemap_fdatawait_range(superblock_info.node_inode->i_mapping, 0, LONG_MAX);
  // filemap_fdatawait_range(superblock_info.meta_inode->i_mapping, 0, LONG_MAX);
#endif

  /* update user_block_counts */
  superblock_info.SetLastValidBlockCount(superblock_info.GetTotalValidBlockCount());
  superblock_info.SetAllocValidBlockCount(0);

  /* Here, we only have one bio having CP pack */
#if 0  // porting needed
  // if (superblock_info.ckpt.ckpt_flags & kCpErrorFlag)
  //	superblock_info->sb->s_flags |= MS_RDONLY;
  // else
#endif
  SyncMetaPages(PageType::kMetaFlush, LONG_MAX);

  GetSegmentManager().ClearPrefreeSegments();
  superblock_info.ClearDirty();
}

// We guarantee that this checkpoint procedure should not fail.
void F2fs::WriteCheckpoint(bool blocked, bool is_umount) {
  SuperblockInfo &superblock_info = GetSuperblockInfo();
  Checkpoint &ckpt = superblock_info.GetCheckpoint();
  uint64_t ckpt_ver;

  fbl::AutoLock cp_lock(&superblock_info.GetCheckpointMutex());
  BlockOperations();

#if 0  // porting needed (bio[type] is empty)
  // GetSegmentManager().SubmitBio(PageType::kData, true);
  // GetSegmentManager().SubmitBio(PageType::kNode, true);
  // GetSegmentManager().SubmitBio(PageType::kMeta, true);
#endif

  // update checkpoint pack index
  // Increase the version number so that
  // SIT entries and seg summaries are written at correct place
  ckpt_ver = LeToCpu(ckpt.checkpoint_ver);
  ckpt.checkpoint_ver = CpuToLe(static_cast<uint64_t>(++ckpt_ver));

  // write cached NAT/SIT entries to NAT/SIT area
  GetNodeManager().FlushNatEntries();
  GetSegmentManager().FlushSitEntries();

  GetSegmentManager().ResetVictimSegmap();

  // unlock all the fs_lock[] in do_checkpoint()
  DoCheckpoint(is_umount);

  UnblockOperations();
}

void F2fs::InitOrphanInfo() {
  SuperblockInfo &superblock_info = GetSuperblockInfo();
  list_initialize(&superblock_info.GetOrphanInodeList());
  superblock_info.ResetNrOrphans();
}

#if 0  // porting needed
// int F2fs::CreateCheckpointCaches() {
//   orphan_entry_slab = KmemCacheCreate("f2fs_orphan_entry",
//   		sizeof(OrphanInodeEntry), nullptr);
//   if (unlikely(!orphan_entry_slab))
//   	return -ENOMEM;
//   inode_entry_slab = KmemCacheCreate("f2fs_dirty_dir_entry",
//   		sizeof(DirInodeEntry), nullptr);
//   if (unlikely(!inode_entry_slab)) {
//   	kmem_cache_destroy(orphan_entry_slab);
//   	return -ENOMEM;
//   }
//   return 0;
// }

// void F2fs::DestroyCheckpointCaches(void) {
//   // kmem_cache_destroy(orphan_entry_slab);
//   // kmem_cache_destroy(inode_entry_slab);
// }
#endif

}  // namespace f2fs
