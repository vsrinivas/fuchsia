// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/f2fs/f2fs.h"

namespace f2fs {

// We guarantee no failure on the returned page.
zx_status_t F2fs::GrabMetaPage(pgoff_t index, fbl::RefPtr<Page> *out) {
  if (zx_status_t ret = GetMetaVnode().GrabCachePage(index, out); ret != ZX_OK) {
    ZX_ASSERT(0);
    return ZX_ERR_NO_MEMORY;
  }
  // We wait writeback only inside GrabMetaPage()
  (*out)->WaitOnWriteback();
  (*out)->SetUptodate();
  return ZX_OK;
}

zx_status_t F2fs::GetMetaPage(pgoff_t index, fbl::RefPtr<Page> *out) {
  if (zx_status_t ret = GetMetaVnode().GrabCachePage(index, out); ret != ZX_OK) {
    ZX_ASSERT(0);
    return ZX_ERR_NO_MEMORY;
  }
  if (zx_status_t err = MakeOperation(storage::OperationType::kRead, *out,
                                      static_cast<block_t>(index), PageType::kMeta);
      err != ZX_OK) {
    Page::PutPage(std::move(*out), true);
    ZX_ASSERT(0);
    return ZX_ERR_IO;
  }
#if 0  // porting needed
  // mark_page_accessed(page);
#endif
  return ZX_OK;
}

zx_status_t F2fs::F2fsWriteMetaPage(fbl::RefPtr<Page> page, bool is_reclaim) {
  zx_status_t err = ZX_OK;

  page->WaitOnWriteback();

  if (page->ClearDirtyForIo(true)) {
    page->SetWriteback();

    if (err = this->GetSegmentManager().WriteMetaPage(std::move(page), is_reclaim); err != ZX_OK) {
#if 0  // porting needed
    // ++wbc->pages_skipped;
#endif
      return err;
    }
  }

  // In this case, we should not unlock this page
#if 0  // porting needed
  // if (err != kAopWritepageActivate)
  // unlock_page(page);
#endif
  return err;
}

pgoff_t F2fs::SyncMetaPages(WritebackOperation &operation) {
  if (superblock_info_->GetPageCount(CountType::kDirtyMeta) == 0 && !operation.bReleasePages) {
    return 0;
  }
  return GetMetaVnode().Writeback(operation);
}

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
  if (superblock_info.GetOrphanCount() >= max_orphans) {
    err = ZX_ERR_NO_SPACE;
    inspect_tree_.OnOutOfSpace();
  }
  return err;
}

void F2fs::AddOrphanInode(VnodeF2fs *vnode) {
  AddOrphanInode(vnode->GetKey());
#ifdef __Fuchsia__
  if (vnode->IsDir()) {
    vnode->Notify(vnode->GetName(), fuchsia_io::wire::kWatchEventDeleted);
  }
#endif  // __Fuchsia__
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
  vnode.reset();
}

zx_status_t F2fs::RecoverOrphanInodes() {
  SuperblockInfo &superblock_info = GetSuperblockInfo();
  block_t start_blk, orphan_blkaddr;

  if (!(superblock_info.GetCheckpoint().ckpt_flags & kCpOrphanPresentFlag))
    return ZX_OK;
  superblock_info.SetOnRecovery();
  start_blk = superblock_info.StartCpAddr() + LeToCpu(raw_sb_->cp_payload) + 1;
  orphan_blkaddr = superblock_info.StartSumAddr() - 1;

  for (block_t i = 0; i < orphan_blkaddr; ++i) {
    fbl::RefPtr<Page> page;
    GetMetaPage(start_blk + i, &page);

    OrphanBlock *orphan_blk;

    orphan_blk = static_cast<OrphanBlock *>(page->GetAddress());
    uint32_t entry_count = LeToCpu(orphan_blk->entry_count);
    // TODO: Need to set NeedChkp flag to repair the fs when fsck repair is available.
    // For now, we trigger assertion.
    ZX_ASSERT(entry_count <= kOrphansPerBlock);
    for (block_t j = 0; j < entry_count; ++j) {
      nid_t ino = LeToCpu(orphan_blk->ino[j]);
      RecoverOrphanInode(ino);
    }
    Page::PutPage(std::move(page), true);
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
  fbl::RefPtr<Page> page;
  uint32_t nentries = 0;
  uint16_t index = 1;
  uint16_t orphan_blocks;

  orphan_blocks = static_cast<uint16_t>(
      (superblock_info.GetOrphanCount() + (kOrphansPerBlock - 1)) / kOrphansPerBlock);

  std::lock_guard lock(superblock_info.GetOrphanInodeMutex());
  head = &superblock_info.GetOrphanInodeList();

  // loop for each orphan inode entry and write them in Jornal block
  list_for_every_safe(head, this_node, next) {
    OrphanInodeEntry *orphan;

    orphan = containerof(this_node, OrphanInodeEntry, list);

    if (nentries == kOrphansPerBlock) {
      // an orphan block is full of 1020 entries,
      // then we need to flush current orphan blocks
      // and bring another one in memory
      orphan_blk->blk_addr = CpuToLe(index);
      orphan_blk->blk_count = CpuToLe(orphan_blocks);
      orphan_blk->entry_count = CpuToLe(nentries);
      page->SetDirty();
      Page::PutPage(std::move(page), true);
      ++index;
      ++start_blk;
      nentries = 0;
    }
    if (!page) {
      GrabMetaPage(start_blk, &page);
      orphan_blk = static_cast<OrphanBlock *>(page->GetAddress());
      memset(orphan_blk, 0, sizeof(*orphan_blk));
      page->SetDirty();
    }
    orphan_blk->ino[nentries++] = CpuToLe(orphan->ino);
  }
  if (page) {
    orphan_blk->blk_addr = CpuToLe(index);
    orphan_blk->blk_count = CpuToLe(orphan_blocks);
    orphan_blk->entry_count = CpuToLe(nentries);
    page->SetDirty();
    Page::PutPage(std::move(page), true);
  }
}

zx_status_t F2fs::ValidateCheckpoint(block_t cp_addr, uint64_t *version, fbl::RefPtr<Page> *out) {
  fbl::RefPtr<Page> cp_page_1, cp_page_2;
  uint64_t blk_size = superblock_info_->GetBlocksize();
  Checkpoint *cp_block;
  uint64_t cur_version = 0, pre_version = 0;
  uint32_t crc = 0;
  size_t crc_offset;
  auto put_pages = fit::defer([&] {
    if (cp_page_2) {
      Page::PutPage(std::move(cp_page_2), true);
    }
    if (cp_page_1) {
      Page::PutPage(std::move(cp_page_1), true);
    }
  });

  // Read the 1st cp block in this CP pack
  GetMetaPage(cp_addr, &cp_page_1);

  // get the version number
  cp_block = static_cast<Checkpoint *>(cp_page_1->GetAddress());
  crc_offset = LeToCpu(cp_block->checksum_offset);
  if (crc_offset >= blk_size) {
    return ZX_ERR_BAD_STATE;
  }

  crc = *reinterpret_cast<uint32_t *>(reinterpret_cast<uint8_t *>(cp_block) + crc_offset);
  if (!F2fsCrcValid(crc, cp_block, static_cast<uint32_t>(crc_offset))) {
    return ZX_ERR_BAD_STATE;
  }

  pre_version = LeToCpu(cp_block->checkpoint_ver);

  // Read the 2nd cp block in this CP pack
  cp_addr += LeToCpu(cp_block->cp_pack_total_block_count) - 1;
  GetMetaPage(cp_addr, &cp_page_2);

  cp_block = static_cast<Checkpoint *>(cp_page_2->GetAddress());
  crc_offset = LeToCpu(cp_block->checksum_offset);
  if (crc_offset >= blk_size) {
    return ZX_ERR_BAD_STATE;
  }

  crc = *reinterpret_cast<uint32_t *>(reinterpret_cast<uint8_t *>(cp_block) + crc_offset);
  if (!F2fsCrcValid(crc, cp_block, static_cast<uint32_t>(crc_offset))) {
    return ZX_ERR_BAD_STATE;
  }

  cur_version = LeToCpu(cp_block->checkpoint_ver);

  if (cur_version == pre_version) {
    put_pages.cancel();
    *version = cur_version;
    Page::PutPage(std::move(cp_page_2), true);
    *out = std::move(cp_page_1);
    return ZX_OK;
  }
  return ZX_ERR_BAD_STATE;
}

zx_status_t F2fs::GetValidCheckpoint() {
  Checkpoint *cp_block;
  Superblock &fsb = RawSb();
  fbl::RefPtr<Page> cp1, cp2;
  Page *cur_page = nullptr;
  uint64_t blk_size = superblock_info_->GetBlocksize();
  uint64_t cp1_version = 0, cp2_version = 0;
  block_t cp_start_blk_no;

  /*
   * Finding out valid cp block involves read both
   * sets( cp pack1 and cp pack 2)
   */
  cp_start_blk_no = LeToCpu(fsb.cp_blkaddr);
  ValidateCheckpoint(cp_start_blk_no, &cp1_version, &cp1);

  /* The second checkpoint pack should start at the next segment */
  cp_start_blk_no += 1 << LeToCpu(fsb.log_blocks_per_seg);
  ValidateCheckpoint(cp_start_blk_no, &cp2_version, &cp2);

  if (cp1 && cp2) {
    if (VerAfter(cp2_version, cp1_version)) {
      cur_page = cp2.get();
    } else {
      cur_page = cp1.get();
      cp_start_blk_no = LeToCpu(fsb.cp_blkaddr);
    }
  } else if (cp1) {
    cur_page = cp1.get();
    cp_start_blk_no = LeToCpu(fsb.cp_blkaddr);
  } else if (cp2) {
    cur_page = cp2.get();
  } else {
    return ZX_ERR_INVALID_ARGS;
  }

  cp_block = static_cast<Checkpoint *>(cur_page->GetAddress());
  memcpy(&superblock_info_->GetCheckpoint(), cp_block, blk_size);

  std::vector<FsBlock> checkpoint_trailer(fsb.cp_payload);
  for (uint32_t i = 0; i < LeToCpu(fsb.cp_payload); ++i) {
    fbl::RefPtr<Page> cp_page;
    GetMetaPage(cp_start_blk_no + 1 + i, &cp_page);
    memcpy(&checkpoint_trailer[i], cp_page->GetAddress(), blk_size);
    Page::PutPage(std::move(cp_page), true);
  }
  superblock_info_->SetCheckpointTrailer(std::move(checkpoint_trailer));

  Page::PutPage(std::move(cp1), true);
  Page::PutPage(std::move(cp2), true);
  return ZX_OK;
}

pgoff_t F2fs::SyncDirtyDataPages(WritebackOperation &operation) {
  pgoff_t nwritten = 0;
  GetVCache().ForDirtyVnodesIf(
      [&](fbl::RefPtr<VnodeF2fs> &vnode) {
        if (!vnode->ShouldFlush()) {
          GetVCache().RemoveDirty(vnode.get());
        } else if (vnode->GetDirtyPageCount()) {
          nwritten += vnode->Writeback(operation);
        }
        if (!operation.to_write) {
          return ZX_ERR_STOP;
        }
        return ZX_OK;
      },
      std::move(operation.if_vnode));
  return nwritten;
}

// Freeze all the FS-operations for checkpoint.
void F2fs::BlockOperations() __TA_NO_THREAD_SAFETY_ANALYSIS {
  SuperblockInfo &superblock_info = GetSuperblockInfo();
  while (true) {
    // write out all the dirty dentry pages
    WritebackOperation op = {.bSync = false};
    op.if_vnode = [](fbl::RefPtr<VnodeF2fs> &vnode) {
      if (vnode->IsDir()) {
        return ZX_OK;
      }
      return ZX_ERR_NEXT;
    };
    SyncDirtyDataPages(op);

    // Stop file operation
    superblock_info.mutex_lock_op(LockType::kFileOp);
    if (superblock_info.GetPageCount(CountType::kDirtyDents)) {
      superblock_info.mutex_unlock_op(LockType::kFileOp);
    } else {
      break;
    }
  }

  // POR: we should ensure that there is no dirty node pages
  // until finishing nat/sit flush.
  while (true) {
    WritebackOperation op = {.bSync = false};
    GetNodeManager().SyncNodePages(op);

    superblock_info.mutex_lock_op(LockType::kNodeOp);
    if (superblock_info.GetPageCount(CountType::kDirtyNodes)) {
      superblock_info.mutex_unlock_op(LockType::kNodeOp);
    } else {
      break;
    }
  }
}

void F2fs::UnblockOperations() __TA_NO_THREAD_SAFETY_ANALYSIS {
  SuperblockInfo &superblock_info = GetSuperblockInfo();
  superblock_info.mutex_unlock_op(LockType::kNodeOp);
  superblock_info.mutex_unlock_op(LockType::kFileOp);
}

void F2fs::DoCheckpoint(bool is_umount) {
  SuperblockInfo &superblock_info = GetSuperblockInfo();
  Checkpoint &ckpt = superblock_info.GetCheckpoint();
  nid_t last_nid = 0;
  block_t start_blk;
  uint32_t data_sum_blocks, orphan_blocks;
  uint32_t crc32 = 0;

  // Flush all the NAT/SIT pages
  while (superblock_info.GetPageCount(CountType::kDirtyMeta)) {
    WritebackOperation op = {.bSync = false};
    SyncMetaPages(op);
  }

  ScheduleWriterSubmitPages();
  GetNodeManager().NextFreeNid(&last_nid);

  // modify checkpoint
  // version number is already updated
  ckpt.elapsed_time = CpuToLe(static_cast<uint64_t>(GetSegmentManager().GetMtime()));
  ckpt.valid_block_count = CpuToLe(ValidUserBlocks());
  ckpt.free_segment_count = CpuToLe(GetSegmentManager().FreeSegments());
  for (int i = 0; i < 3; ++i) {
    ckpt.cur_node_segno[i] =
        CpuToLe(GetSegmentManager().CursegSegno(i + static_cast<int>(CursegType::kCursegHotNode)));
    ckpt.cur_node_blkoff[i] =
        CpuToLe(GetSegmentManager().CursegBlkoff(i + static_cast<int>(CursegType::kCursegHotNode)));
    ckpt.alloc_type[i + static_cast<int>(CursegType::kCursegHotNode)] =
        GetSegmentManager().CursegAllocType(i + static_cast<int>(CursegType::kCursegHotNode));
  }
  for (int i = 0; i < 3; ++i) {
    ckpt.cur_data_segno[i] =
        CpuToLe(GetSegmentManager().CursegSegno(i + static_cast<int>(CursegType::kCursegHotData)));
    ckpt.cur_data_blkoff[i] =
        CpuToLe(GetSegmentManager().CursegBlkoff(i + static_cast<int>(CursegType::kCursegHotData)));
    ckpt.alloc_type[i + static_cast<int>(CursegType::kCursegHotData)] =
        GetSegmentManager().CursegAllocType(i + static_cast<int>(CursegType::kCursegHotData));
  }

  ckpt.valid_node_count = CpuToLe(ValidNodeCount());
  ckpt.valid_inode_count = CpuToLe(ValidInodeCount());
  ckpt.next_free_nid = CpuToLe(last_nid);

  // 2 cp  + n data seg summary + orphan inode blocks
  data_sum_blocks = GetSegmentManager().NpagesForSummaryFlush();
  if (data_sum_blocks < 3) {
    ckpt.ckpt_flags |= kCpCompactSumFlag;
  } else {
    ckpt.ckpt_flags &= (~kCpCompactSumFlag);
  }

  orphan_blocks = static_cast<uint32_t>((superblock_info.GetOrphanCount() + kOrphansPerBlock - 1) /
                                        kOrphansPerBlock);
  ckpt.cp_pack_start_sum = 1 + orphan_blocks + LeToCpu(raw_sb_->cp_payload);
  ckpt.cp_pack_total_block_count =
      2 + data_sum_blocks + orphan_blocks + LeToCpu(raw_sb_->cp_payload);

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

  // update SIT/NAT bitmap
  GetSegmentManager().GetSitBitmap(superblock_info.BitmapPtr(MetaBitmap::kSitBitmap));
  GetNodeManager().GetNatBitmap(superblock_info.BitmapPtr(MetaBitmap::kNatBitmap));

  crc32 = CpuToLe(F2fsCrc32(&ckpt, LeToCpu(ckpt.checksum_offset)));
  memcpy(reinterpret_cast<uint8_t *>(&ckpt) + LeToCpu(ckpt.checksum_offset), &crc32,
         sizeof(uint32_t));

  start_blk = superblock_info.StartCpAddr();

  // write out checkpoint buffer at block 0
  fbl::RefPtr<Page> cp_page;
  GrabMetaPage(start_blk++, &cp_page);
  memcpy(cp_page->GetAddress(), &ckpt, (1 << superblock_info.GetLogBlocksize()));
  cp_page->SetDirty();
  Page::PutPage(std::move(cp_page), true);

  for (uint32_t i = 0; i < LeToCpu(raw_sb_->cp_payload); ++i) {
    GrabMetaPage(start_blk++, &cp_page);
    memcpy(cp_page->GetAddress(), &superblock_info.GetCheckpointTrailer()[i],
           (1 << superblock_info.GetLogBlocksize()));
    cp_page->SetDirty();
    Page::PutPage(std::move(cp_page), true);
  }

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

  // writeout checkpoint block
  GrabMetaPage(start_blk, &cp_page);
  memcpy(cp_page->GetAddress(), &ckpt, (1 << superblock_info.GetLogBlocksize()));
  cp_page->SetDirty();
  Page::PutPage(std::move(cp_page), true);

  // wait for previous submitted node/meta pages writeback
  sync_completion_t completion;
  ScheduleWriterSubmitPages(&completion);
  sync_completion_wait(&completion, ZX_TIME_INFINITE);
  ZX_ASSERT(superblock_info.GetPageCount(CountType::kWriteback) == 0);

  // update user_block_counts
  superblock_info.SetLastValidBlockCount(superblock_info.GetTotalValidBlockCount());
  superblock_info.SetAllocValidBlockCount(0);

  // Here, we only have dirty meta Pages for CP pack
  WritebackOperation op = {.bSync = true};
  SyncMetaPages(op);
  // TODO: Release resource in VnodeCache, FileCache

#if 0  // porting needed
  // if (superblock_info.ckpt.ckpt_flags & kCpErrorFlag)
  //	superblock_info->sb->s_flags |= MS_RDONLY;
#endif

  GetSegmentManager().ClearPrefreeSegments();
  superblock_info.ClearDirty();

  ZX_ASSERT(superblock_info_->GetPageCount(CountType::kDirtyMeta) == 0);
  meta_vnode_->InvalidatePages();
}

// We guarantee that this checkpoint procedure should not fail.
void F2fs::WriteCheckpoint(bool blocked, bool is_umount) {
  SuperblockInfo &superblock_info = GetSuperblockInfo();
  Checkpoint &ckpt = superblock_info.GetCheckpoint();
  uint64_t ckpt_ver;

  std::lock_guard cp_lock(superblock_info.GetCheckpointMutex());
  BlockOperations();

  ScheduleWriterSubmitPages();

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

  if (is_umount) {
    ZX_ASSERT(superblock_info_->GetPageCount(CountType::kDirtyDents) == 0);
    ZX_ASSERT(superblock_info_->GetPageCount(CountType::kDirtyData) == 0);
    ZX_ASSERT(superblock_info_->GetPageCount(CountType::kWriteback) == 0);
    ZX_ASSERT(superblock_info_->GetPageCount(CountType::kDirtyMeta) == 0);
    ZX_ASSERT(superblock_info_->GetPageCount(CountType::kDirtyNodes) == 0);
  }
  UnblockOperations();
}

void F2fs::InitOrphanInfo() {
  SuperblockInfo &superblock_info = GetSuperblockInfo();
  list_initialize(&superblock_info.GetOrphanInodeList());
  superblock_info.ResetNrOrphans();
}

}  // namespace f2fs
