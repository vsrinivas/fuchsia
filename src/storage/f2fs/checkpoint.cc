// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/f2fs/f2fs.h"

namespace f2fs {

// TODO: guarantee no failure on the returned page.
zx_status_t F2fs::GrabMetaPage(pgoff_t index, LockedPage *out) {
  if (zx_status_t ret = GetMetaVnode().GrabCachePage(index, out); ret != ZX_OK) {
    ZX_ASSERT_MSG(false, "GrabMetaPage() fails [addr: 0x%lx, ret: %d]\n", index, ret);
    return ret;
  }
  // We wait writeback only inside GrabMetaPage()
  (*out)->WaitOnWriteback();
  (*out)->SetUptodate();
  return ZX_OK;
}

zx_status_t F2fs::GetMetaPage(pgoff_t index, LockedPage *out) {
  LockedPage page;
  if (zx_status_t ret = GetMetaVnode().GrabCachePage(index, &page); ret != ZX_OK) {
    ZX_ASSERT_MSG(false, "GetMetaPage() fails [addr: 0x%lx, ret: %d]\n", index, ret);
    return ret;
  }

  auto page_or =
      MakeReadOperation(std::move(page), safemath::checked_cast<block_t>(index), PageType::kMeta);
  if (page_or.is_error()) {
    return page_or.status_value();
  }
#if 0  // porting needed
  // mark_page_accessed(page);
#endif
  *out = std::move(*page_or);
  return ZX_OK;
}

zx_status_t F2fs::F2fsWriteMetaPage(LockedPage &page, bool is_reclaim) const {
  zx_status_t err = ZX_OK;

  page->WaitOnWriteback();

  if (page->ClearDirtyForIo()) {
    page->SetWriteback();

    if (err = this->GetSegmentManager().WriteMetaPage(page, is_reclaim); err != ZX_OK) {
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
  if (superblock_info.GetVnodeSetSize(InoType::kOrphanIno) >= max_orphans) {
    err = ZX_ERR_NO_SPACE;
#ifdef __Fuchsia__
    inspect_tree_->OnOutOfSpace();
#endif  // __Fuchsia__
  }
  return err;
}

void F2fs::AddOrphanInode(VnodeF2fs *vnode) {
  GetSuperblockInfo().AddVnodeToVnodeSet(InoType::kOrphanIno, vnode->GetKey());
#ifdef __Fuchsia__
  if (vnode->IsDir()) {
    vnode->Notify(".", fuchsia_io::wire::WatchEvent::kDeleted);
  }
#endif  // __Fuchsia__
  if (vnode->ClearDirty()) {
    // Set the orphan flag of filecache to prevent further dirty Pages.
    vnode->ClearDirtyPages();
    ZX_ASSERT(GetVCache().RemoveDirty(vnode) == ZX_OK);
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

  if (!(superblock_info.TestCpFlags(CpFlag::kCpOrphanPresentFlag)))
    return ZX_OK;
  superblock_info.SetOnRecovery();
  start_blk = superblock_info.StartCpAddr() + LeToCpu(raw_sb_->cp_payload) + 1;
  orphan_blkaddr = superblock_info.StartSumAddr() - 1;

  for (block_t i = 0; i < orphan_blkaddr; ++i) {
    LockedPage page;
    GetMetaPage(start_blk + i, &page);

    OrphanBlock *orphan_blk;

    orphan_blk = page->GetAddress<OrphanBlock>();
    uint32_t entry_count = LeToCpu(orphan_blk->entry_count);
    // TODO: Need to set NeedChkp flag to repair the fs when fsck repair is available.
    // For now, we trigger assertion.
    ZX_ASSERT(entry_count <= kOrphansPerBlock);
    for (block_t j = 0; j < entry_count; ++j) {
      nid_t ino = LeToCpu(orphan_blk->ino[j]);
      RecoverOrphanInode(ino);
    }
  }
  // clear Orphan Flag
  superblock_info.ClearCpFlags(CpFlag::kCpOrphanPresentFlag);
  superblock_info.ClearOnRecovery();
  return ZX_OK;
}

void F2fs::WriteOrphanInodes(block_t start_blk) {
  SuperblockInfo &superblock_info = GetSuperblockInfo();
  OrphanBlock *orphan_blk = nullptr;
  LockedPage page;
  uint32_t nentries = 0;
  uint16_t index = 1;
  uint16_t orphan_blocks;

  orphan_blocks = static_cast<uint16_t>(
      (superblock_info.GetVnodeSetSize(InoType::kOrphanIno) + (kOrphansPerBlock - 1)) /
      kOrphansPerBlock);

  superblock_info.ForAllVnodesInVnodeSet(InoType::kOrphanIno, [&](nid_t ino) {
    if (nentries == kOrphansPerBlock) {
      // an orphan block is full of 1020 entries,
      // then we need to flush current orphan blocks
      // and bring another one in memory
      orphan_blk->blk_addr = CpuToLe(index);
      orphan_blk->blk_count = CpuToLe(orphan_blocks);
      orphan_blk->entry_count = CpuToLe(nentries);
      page->SetDirty();
      page.reset();
      ++index;
      ++start_blk;
      nentries = 0;
    }
    if (!page) {
      GrabMetaPage(start_blk, &page);
      orphan_blk = page->GetAddress<OrphanBlock>();
      memset(orphan_blk, 0, sizeof(*orphan_blk));
      page->SetDirty();
    }
    orphan_blk->ino[nentries++] = CpuToLe(ino);
  });
  if (page) {
    orphan_blk->blk_addr = CpuToLe(index);
    orphan_blk->blk_count = CpuToLe(orphan_blocks);
    orphan_blk->entry_count = CpuToLe(nentries);
    page->SetDirty();
  }
}

zx_status_t F2fs::ValidateCheckpoint(block_t cp_addr, uint64_t *version, LockedPage *out) {
  LockedPage cp_page_1, cp_page_2;
  uint64_t blk_size = superblock_info_->GetBlocksize();
  Checkpoint *cp_block;
  uint64_t cur_version = 0, pre_version = 0;
  uint32_t crc = 0;
  size_t crc_offset;

  // Read the 1st cp block in this CP pack
  GetMetaPage(cp_addr, &cp_page_1);

  // get the version number
  cp_block = cp_page_1->GetAddress<Checkpoint>();
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

  cp_block = cp_page_2->GetAddress<Checkpoint>();
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
    *version = cur_version;
    *out = std::move(cp_page_1);
    return ZX_OK;
  }
  return ZX_ERR_BAD_STATE;
}

zx_status_t F2fs::GetValidCheckpoint() {
  Checkpoint *cp_block;
  Superblock &fsb = RawSb();
  LockedPage cp1, cp2;
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

  cp_block = cur_page->GetAddress<Checkpoint>();
  memcpy(&superblock_info_->GetCheckpoint(), cp_block, blk_size);

  std::vector<FsBlock> checkpoint_trailer(fsb.cp_payload);
  for (uint32_t i = 0; i < LeToCpu(fsb.cp_payload); ++i) {
    LockedPage cp_page;
    GetMetaPage(cp_start_blk_no + 1 + i, &cp_page);
    memcpy(&checkpoint_trailer[i], cp_page->GetAddress(), blk_size);
  }
  superblock_info_->SetCheckpointTrailer(std::move(checkpoint_trailer));

  return ZX_OK;
}

pgoff_t F2fs::SyncDirtyDataPages(WritebackOperation &operation) {
  pgoff_t total_nwritten = 0;
  GetVCache().ForDirtyVnodesIf(
      [&](fbl::RefPtr<VnodeF2fs> &vnode) {
        if (!vnode->ShouldFlush()) {
          GetVCache().RemoveDirty(vnode.get());
        } else if (vnode->GetDirtyPageCount()) {
          auto nwritten = vnode->Writeback(operation);
          total_nwritten = safemath::CheckAdd<pgoff_t>(total_nwritten, nwritten).ValueOrDie();
          if (nwritten >= operation.to_write) {
            return ZX_ERR_STOP;
          } else {
            operation.to_write -= nwritten;
          }
        }
        return ZX_OK;
      },
      std::move(operation.if_vnode));
  return total_nwritten;
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

void F2fs::UnblockOperations() const __TA_NO_THREAD_SAFETY_ANALYSIS {
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
    superblock_info.SetCpFlags(CpFlag::kCpCompactSumFlag);
  } else {
    superblock_info.ClearCpFlags(CpFlag::kCpCompactSumFlag);
  }

  orphan_blocks = static_cast<uint32_t>(
      (superblock_info.GetVnodeSetSize(InoType::kOrphanIno) + kOrphansPerBlock - 1) /
      kOrphansPerBlock);
  ckpt.cp_pack_start_sum = 1 + orphan_blocks + LeToCpu(raw_sb_->cp_payload);
  ckpt.cp_pack_total_block_count =
      2 + data_sum_blocks + orphan_blocks + LeToCpu(raw_sb_->cp_payload);

  if (is_umount) {
    superblock_info.SetCpFlags(CpFlag::kCpUmountFlag);
    ckpt.cp_pack_total_block_count += kNrCursegNodeType;
  } else {
    superblock_info.ClearCpFlags(CpFlag::kCpUmountFlag);
  }

  if (superblock_info.GetVnodeSetSize(InoType::kOrphanIno) > 0) {
    superblock_info.SetCpFlags(CpFlag::kCpOrphanPresentFlag);
  } else {
    superblock_info.ClearCpFlags(CpFlag::kCpOrphanPresentFlag);
  }

  // update SIT/NAT bitmap
  GetSegmentManager().GetSitBitmap(superblock_info.BitmapPtr(MetaBitmap::kSitBitmap));
  GetNodeManager().GetNatBitmap(superblock_info.BitmapPtr(MetaBitmap::kNatBitmap));

  crc32 = CpuToLe(F2fsCrc32(&ckpt, LeToCpu(ckpt.checksum_offset)));
  memcpy(reinterpret_cast<uint8_t *>(&ckpt) + LeToCpu(ckpt.checksum_offset), &crc32,
         sizeof(uint32_t));

  start_blk = superblock_info.StartCpAddr();

  // Prepare Pages for this checkpoint pack
  {
    LockedPage cp_page;
    GrabMetaPage(start_blk++, &cp_page);
    memcpy(cp_page->GetAddress(), &ckpt, (1 << superblock_info.GetLogBlocksize()));
    cp_page->SetDirty();
  }

  for (uint32_t i = 0; i < LeToCpu(raw_sb_->cp_payload); ++i) {
    LockedPage cp_page;
    GrabMetaPage(start_blk++, &cp_page);
    memcpy(cp_page->GetAddress(), &superblock_info.GetCheckpointTrailer()[i],
           (1 << superblock_info.GetLogBlocksize()));
    cp_page->SetDirty();
  }

  if (superblock_info.GetVnodeSetSize(InoType::kOrphanIno) > 0) {
    WriteOrphanInodes(start_blk);
    start_blk += orphan_blocks;
  }

  GetSegmentManager().WriteDataSummaries(start_blk);
  start_blk += data_sum_blocks;
  if (is_umount) {
    GetSegmentManager().WriteNodeSummaries(start_blk);
    start_blk += kNrCursegNodeType;
  }

  {
    // Write out this checkpoint pack.
    WritebackOperation op = {.bSync = true};
    SyncMetaPages(op);
  }

  // Prepare the commit block.
  {
    LockedPage cp_page;
    GrabMetaPage(start_blk, &cp_page);
    memcpy(cp_page->GetAddress(), &ckpt, (1 << superblock_info.GetLogBlocksize()));
    cp_page->SetDirty();
  }

  // Update the valid block count.
  superblock_info.SetLastValidBlockCount(superblock_info.GetTotalValidBlockCount());
  superblock_info.SetAllocValidBlockCount(0);

  // Commit.
  if (!superblock_info.TestCpFlags(CpFlag::kCpErrorFlag)) {
    ZX_ASSERT(superblock_info.GetPageCount(CountType::kWriteback) == 0);
    ZX_ASSERT(superblock_info.GetPageCount(CountType::kDirtyMeta) == 1);
    // TODO: Use FUA when it is available.
    GetBc().Flush();
    WritebackOperation op = {.bSync = true};
    SyncMetaPages(op);
    GetBc().Flush();

    GetSegmentManager().ClearPrefreeSegments();
    superblock_info.ClearDirty();
    meta_vnode_->InvalidatePages();
  }
}

uint32_t F2fs::GetFreeSectionsForDirtyPages() {
  uint32_t pages_per_sec =
      safemath::CheckMul<uint32_t>((1 << superblock_info_->GetLogBlocksPerSeg()),
                                   superblock_info_->GetSegsPerSec())
          .ValueOrDie();
  uint32_t node_secs =
      safemath::CheckDiv<uint32_t>(
          ((superblock_info_->GetPageCount(CountType::kDirtyNodes) + pages_per_sec - 1) >>
           superblock_info_->GetLogBlocksPerSeg()),
          superblock_info_->GetSegsPerSec())
          .ValueOrDie();
  uint32_t dent_secs =
      safemath::CheckDiv<uint32_t>(
          ((superblock_info_->GetPageCount(CountType::kDirtyDents) + pages_per_sec - 1) >>
           superblock_info_->GetLogBlocksPerSeg()),
          superblock_info_->GetSegsPerSec())
          .ValueOrDie();

  return (node_secs + safemath::CheckMul<uint32_t>(dent_secs, 2)).ValueOrDie();
}

bool F2fs::IsCheckpointAvailable() {
  return segment_manager_->FreeSections() > GetFreeSectionsForDirtyPages();
}

// Release-acquire ordering between the writeback (loader) and others such as checkpoint and gc.
bool F2fs::CanReclaim() const { return !stop_reclaim_flag_.test(std::memory_order_acquire); }
bool F2fs::IsTearDown() const { return teardown_flag_.test(std::memory_order_relaxed); }
void F2fs::SetTearDown() { teardown_flag_.test_and_set(std::memory_order_relaxed); }

// We guarantee that this checkpoint procedure should not fail.
void F2fs::WriteCheckpoint(bool blocked, bool is_umount) {
  SuperblockInfo &superblock_info = GetSuperblockInfo();
  Checkpoint &ckpt = superblock_info.GetCheckpoint();
  uint64_t ckpt_ver;

  if (superblock_info.TestCpFlags(CpFlag::kCpErrorFlag)) {
    return;
  }

  std::lock_guard cp_lock(checkpoint_mutex_);
  // Stop writeback during checkpoint.
  FlagAcquireGuard flag(&stop_reclaim_flag_);
  if (flag.IsAcquired()) {
    ZX_ASSERT(WaitForWriteback().is_ok());
  }
  ZX_DEBUG_ASSERT(IsCheckpointAvailable());
  BlockOperations();

  // update checkpoint pack index
  // Increase the version number so that
  // SIT entries and seg summaries are written at correct place
  ckpt_ver = LeToCpu(ckpt.checkpoint_ver);
  ckpt.checkpoint_ver = CpuToLe(static_cast<uint64_t>(++ckpt_ver));

  // write cached NAT/SIT entries to NAT/SIT area
  GetNodeManager().FlushNatEntries();
  GetSegmentManager().FlushSitEntries();

  // unlock all the fs_lock[] in do_checkpoint()
  DoCheckpoint(is_umount);

  if (is_umount && !(superblock_info.TestCpFlags(CpFlag::kCpErrorFlag))) {
    ZX_ASSERT(superblock_info_->GetPageCount(CountType::kDirtyDents) == 0);
    ZX_ASSERT(superblock_info_->GetPageCount(CountType::kDirtyData) == 0);
    ZX_ASSERT(superblock_info_->GetPageCount(CountType::kWriteback) == 0);
    ZX_ASSERT(superblock_info_->GetPageCount(CountType::kDirtyMeta) == 0);
    ZX_ASSERT(superblock_info_->GetPageCount(CountType::kDirtyNodes) == 0);
  }
  UnblockOperations();
}

}  // namespace f2fs
