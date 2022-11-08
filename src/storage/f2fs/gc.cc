// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/f2fs/f2fs.h"

namespace f2fs {
uint32_t SegmentManager::GetGcCost(uint32_t segno, const VictimSelPolicy &policy) {
  if (policy.alloc_mode == AllocMode::kSSR)
    return GetSegmentEntry(segno).ckpt_valid_blocks;

  if (policy.gc_mode == GcMode::kGcGreedy) {
    return GetGreedyCost(segno);
  } else {
    // TODO: Add GetCbCost() for GcMode::kGcCb when Background GC is implemented.
    return kUint32Max;
  }
}

uint32_t SegmentManager::GetGreedyCost(uint32_t segno) {
  uint32_t valid_blocks = GetValidBlocks(segno, superblock_info_->GetSegsPerSec());

  if (IsDataSeg(static_cast<CursegType>(GetSegmentEntry(segno).type))) {
    return 2 * valid_blocks;
  } else {
    return valid_blocks;
  }
}

VictimSelPolicy SegmentManager::GetVictimSelPolicy(GcType gc_type, CursegType type,
                                                   AllocMode alloc_mode) {
  VictimSelPolicy policy;
  policy.alloc_mode = alloc_mode;
  if (policy.alloc_mode == AllocMode::kSSR) {
    policy.gc_mode = GcMode::kGcGreedy;
    policy.dirty_segmap = dirty_info_->dirty_segmap[static_cast<int>(type)].get();
    policy.max_search = dirty_info_->nr_dirty[static_cast<int>(type)];
    policy.ofs_unit = 1;
  } else {
    policy.gc_mode = (gc_type == GcType::kBgGc) ? GcMode::kGcCb : GcMode::kGcGreedy;
    policy.dirty_segmap = dirty_info_->dirty_segmap[static_cast<int>(DirtyType::kDirty)].get();
    policy.max_search = dirty_info_->nr_dirty[static_cast<int>(DirtyType::kDirty)];
    policy.ofs_unit = superblock_info_->GetSegsPerSec();
  }

  if (policy.max_search > kMaxSearchLimit) {
    policy.max_search = kMaxSearchLimit;
  }

  policy.offset = superblock_info_->GetLastVictim(static_cast<int>(policy.gc_mode));
  return policy;
}

uint32_t SegmentManager::GetMaxCost(const VictimSelPolicy &policy) {
  if (policy.alloc_mode == AllocMode::kSSR)
    return 1 << superblock_info_->GetLogBlocksPerSeg();
  if (policy.gc_mode == GcMode::kGcGreedy)
    return 2 * (1 << superblock_info_->GetLogBlocksPerSeg()) * policy.ofs_unit;
  else if (policy.gc_mode == GcMode::kGcCb)
    return kUint32Max;
  return 0;
}

zx::result<uint32_t> SegmentManager::GetVictimByDefault(GcType gc_type, CursegType type,
                                                        AllocMode alloc_mode) {
  std::lock_guard lock(dirty_info_->seglist_lock);
  VictimSelPolicy policy = GetVictimSelPolicy(gc_type, type, alloc_mode);

  policy.min_segno = kNullSegNo;
  policy.min_cost = GetMaxCost(policy);

  uint32_t nSearched = 0;

  if (policy.max_search == 0) {
    return zx::error(ZX_ERR_UNAVAILABLE);
  }

#if 0  // porting needed
  //  if (p.alloc_mode == AllocMode::kLFS && gc_type == GcType::kFgGc) {
  //    p.min_segno = CheckBgVictims();
  //  }
#endif

  auto gc_mode = static_cast<int>(policy.gc_mode);
  if (policy.min_segno == kNullSegNo) {
    block_t last_segment = TotalSegs();
    while (nSearched < policy.max_search) {
      uint32_t segno = FindNextBit(policy.dirty_segmap, last_segment, policy.offset);
      if (segno >= last_segment) {
        if (superblock_info_->GetLastVictim(gc_mode)) {
          last_segment = superblock_info_->GetLastVictim(gc_mode);
          superblock_info_->SetLastVictim(gc_mode, 0);
          policy.offset = 0;
          continue;
        }
        break;
      }
      policy.offset = segno + policy.ofs_unit;
      uint32_t secno = GetSecNo(segno);

      if (policy.ofs_unit > 1) {
        policy.offset = fbl::round_down(policy.offset, policy.ofs_unit);
        nSearched +=
            CountBits(policy.dirty_segmap, policy.offset - policy.ofs_unit, policy.ofs_unit);
      } else {
        ++nSearched;
      }

      if (SecUsageCheck(secno)) {
        continue;
      }

      if (gc_type == GcType::kBgGc && TestBit(secno, dirty_info_->victim_secmap.get())) {
        continue;
      }

      uint32_t cost = GetGcCost(segno, policy);

      if (policy.min_cost > cost) {
        policy.min_segno = segno;
        policy.min_cost = cost;
      }

      if (cost == GetMaxCost(policy)) {
        continue;
      }

      if (nSearched >= policy.max_search) {
        // It has already checked all or |kMaxSearchLimit| of dirty segments. Set the last victim to
        // |policy.offset| from which the next search will start to find victims.
        superblock_info_->SetLastVictim(
            gc_mode, (safemath::CheckAdd<uint32_t>(segno, 1) % fs_->GetSegmentManager().TotalSegs())
                         .ValueOrDie());
      }
    }
  }

  if (policy.min_segno != kNullSegNo) {
    if (policy.alloc_mode == AllocMode::kLFS) {
      uint32_t secno = GetSecNo(policy.min_segno);
      if (gc_type == GcType::kFgGc) {
        fs_->GetGcManager().SetCurVictimSec(secno);
      } else {
        SetBit(secno, dirty_info_->victim_secmap.get());
      }
    }
    return zx::ok((policy.min_segno / policy.ofs_unit) * policy.ofs_unit);
  }

  return zx::error(ZX_ERR_UNAVAILABLE);
}

zx::result<uint32_t> GcManager::GetGcVictim(GcType gc_type, CursegType type) {
  SitInfo &sit_i = fs_->GetSegmentManager().GetSitInfo();
  std::lock_guard sentry_lock(sit_i.sentry_lock);
  return fs_->GetSegmentManager().GetVictimByDefault(gc_type, type, AllocMode::kLFS);
}

zx::result<uint32_t> GcManager::F2fsGc() {
  // For testing
  if (disable_gc_for_test_) {
    return zx::ok(0);
  }

  if (fs_->GetSuperblockInfo().TestCpFlags(CpFlag::kCpErrorFlag)) {
    return zx::error(ZX_ERR_BAD_STATE);
  }

  std::lock_guard gc_lock(gc_mutex_);

  // TODO: Default gc_type should be kBgGc when background gc is implemented.
  GcType gc_type = GcType::kFgGc;
  uint32_t sec_freed = 0;
  auto &segment_manager = fs_->GetSegmentManager();

  // FG_GC must run when there is no space (e.g., HasNotEnoughFreeSecs() == true).
  // If not, gc can compete with others (e.g., writeback) for victim Pages and space.
  while (segment_manager.HasNotEnoughFreeSecs()) {
    // Stop writeback before gc. The writeback won't be invoked until gc acquires enough sections.
    FlagAcquireGuard flag(&fs_->GetStopReclaimFlag());
    if (flag.IsAcquired()) {
      ZX_ASSERT(fs_->WaitForWriteback().is_ok());
    }

    if (fs_->GetSuperblockInfo().TestCpFlags(CpFlag::kCpErrorFlag)) {
      return zx::error(ZX_ERR_BAD_STATE);
    }
    // For example, if there are many prefree_segments below given threshold, we can make them
    // free by checkpoint. Then, we secure free segments which doesn't need fggc any more.
    if (segment_manager.PrefreeSegments()) {
      auto before = segment_manager.FreeSections();
      fs_->WriteCheckpoint(false, false);
      sec_freed = (safemath::CheckSub<uint32_t>(segment_manager.FreeSections(), before) + sec_freed)
                      .ValueOrDie();
      // After acquiring free sections, check if further gc is necessary.
      continue;
    }

    if (gc_type == GcType::kBgGc && segment_manager.HasNotEnoughFreeSecs()) {
      gc_type = GcType::kFgGc;
    }

    uint32_t segno;
    if (auto ret = GetGcVictim(gc_type, CursegType::kNoCheckType); ret.is_error()) {
      break;
    } else {
      segno = ret.value();
    }

    if (auto err = DoGarbageCollect(segno, gc_type); err != ZX_OK) {
      return zx::error(err);
    }

    if (gc_type == GcType::kFgGc) {
      SetCurVictimSec(kNullSecNo);
      fs_->WriteCheckpoint(false, false);
      ++sec_freed;
    }
  }
  if (!sec_freed) {
    return zx::error(ZX_ERR_UNAVAILABLE);
  }
  return zx::ok(sec_freed);
}

zx_status_t GcManager::DoGarbageCollect(uint32_t start_segno, GcType gc_type) {
  for (uint32_t i = 0; i < fs_->GetSuperblockInfo().GetSegsPerSec(); ++i) {
    uint32_t segno = start_segno + i;
    uint8_t type = fs_->GetSegmentManager().IsDataSeg(static_cast<CursegType>(
                       fs_->GetSegmentManager().GetSegmentEntry(segno).type))
                       ? kSumTypeData
                       : kSumTypeNode;

    if (fs_->GetSegmentManager().GetValidBlocks(segno, 1) == 0) {
      continue;
    }

    fbl::RefPtr<Page> sum_page;
    {
      LockedPage locked_sum_page;
      fs_->GetSegmentManager().GetSumPage(segno, &locked_sum_page);
      sum_page = locked_sum_page.release();
    }

    SummaryBlock *sum_blk = sum_page->GetAddress<SummaryBlock>();
    ZX_DEBUG_ASSERT(type == GetSumType((&sum_blk->footer)));

    if (zx_status_t status = (type == kSumTypeNode) ? GcNodeSegment(*sum_blk, segno, gc_type)
                                                    : GcDataSegment(*sum_blk, segno, gc_type);
        status != ZX_OK) {
      return status;
    }
  }
  return ZX_OK;
}

bool GcManager::CheckValidMap(uint32_t segno, uint64_t offset) {
  SitInfo &sit_info = fs_->GetSegmentManager().GetSitInfo();

  std::shared_lock sentry_lock(sit_info.sentry_lock);
  SegmentEntry &sentry = fs_->GetSegmentManager().GetSegmentEntry(segno);
  return TestValidBitmap(offset, sentry.cur_valid_map.get());
}

zx_status_t GcManager::GcNodeSegment(const SummaryBlock &sum_blk, uint32_t segno, GcType gc_type) {
  const Summary *entry = sum_blk.entries;
  for (block_t off = 0; off < fs_->GetSuperblockInfo().GetBlocksPerSeg(); ++off, ++entry) {
    nid_t nid = CpuToLe(entry->nid);

    if (gc_type == GcType::kBgGc && fs_->GetSegmentManager().HasNotEnoughFreeSecs()) {
      return ZX_ERR_BAD_STATE;
    }

    if (!CheckValidMap(segno, off)) {
      continue;
    }

    LockedPage node_page;
    if (auto err = fs_->GetNodeManager().GetNodePage(nid, &node_page); err != ZX_OK) {
      continue;
    }

    NodeInfo ni;
    fs_->GetNodeManager().GetNodeInfo(nid, ni);
    if (ni.blk_addr != fs_->GetSegmentManager().StartBlock(segno) + off) {
      continue;
    }

    node_page->WaitOnWriteback();
    node_page->SetDirty();
  }

  return ZX_OK;
}

zx::result<std::pair<nid_t, block_t>> GcManager::CheckDnode(const Summary &sum, block_t blkaddr) {
  nid_t nid = LeToCpu(sum.nid);
  uint64_t ofs_in_node = LeToCpu(sum.ofs_in_node);

  LockedPage node_page;
  if (auto err = fs_->GetNodeManager().GetNodePage(nid, &node_page); err != ZX_OK) {
    return zx::error(err);
  }

  NodeInfo dnode_info;
  fs_->GetNodeManager().GetNodeInfo(nid, dnode_info);

  if (sum.version != dnode_info.version) {
    return zx::error(ZX_ERR_BAD_STATE);
  }

  fs_->GetNodeManager().CheckNidRange(dnode_info.ino);

  fbl::RefPtr<VnodeF2fs> vnode;
  if (zx_status_t err = VnodeF2fs::Vget(fs_, dnode_info.ino, &vnode); err != ZX_OK) {
    return zx::error(err);
  }

  auto start_bidx = node_page.GetPage<NodePage>().StartBidxOfNode(*vnode);
  block_t source_blkaddr = DatablockAddr(&node_page.GetPage<NodePage>(), ofs_in_node);

  if (source_blkaddr != blkaddr) {
    return zx::error(ZX_ERR_BAD_STATE);
  }
  return zx::ok(std::make_pair(dnode_info.ino, start_bidx));
}

zx_status_t GcManager::GcDataSegment(const SummaryBlock &sum_blk, unsigned int segno,
                                     GcType gc_type) {
  block_t start_addr = fs_->GetSegmentManager().StartBlock(segno);
  const Summary *entry = sum_blk.entries;
  for (block_t off = 0; off < fs_->GetSuperblockInfo().GetBlocksPerSeg(); ++off, ++entry) {
    // stop BG_GC if there is not enough free sections. Or, stop GC if the section becomes fully
    // valid caused by race condition along with SSR block allocation.
    const uint32_t kBlocksPerSection =
        fs_->GetSuperblockInfo().GetBlocksPerSeg() * fs_->GetSuperblockInfo().GetSegsPerSec();
    const block_t target_address = safemath::CheckAdd<block_t>(start_addr, off).ValueOrDie();
    if ((gc_type == GcType::kBgGc && fs_->GetSegmentManager().HasNotEnoughFreeSecs()) ||
        fs_->GetSegmentManager().GetValidBlocks(segno, fs_->GetSuperblockInfo().GetSegsPerSec()) ==
            kBlocksPerSection) {
      return ZX_ERR_BAD_STATE;
    }

    if (!CheckValidMap(segno, off)) {
      continue;
    }

    auto dnode_result = CheckDnode(*entry, target_address);
    if (dnode_result.is_error()) {
      continue;
    }
    auto [ino, start_bidx] = dnode_result.value();

    uint32_t ofs_in_node = LeToCpu(entry->ofs_in_node);

    fbl::RefPtr<VnodeF2fs> vnode;
    if (auto err = VnodeF2fs::Vget(fs_, ino, &vnode); err != ZX_OK) {
      continue;
    }

    LockedPage data_page;
    if (auto err = vnode->GetLockedDataPage(start_bidx + ofs_in_node, &data_page); err != ZX_OK) {
      continue;
    }

    if (gc_type == GcType::kFgGc &&
        fs_->GetSuperblockInfo().FindVnodeFromVnodeSet(InoType::kOrphanIno, ino)) {
      // Here, GC already uploaded victim data block to the filecache.
      // Once a page of an orphan file is uploaded to the filecache, the page is not reclaimed until
      // the vnode is recycled. Therefore, even if we truncate it here, orphan files that are
      // already opened can access data. If SPO occurs during the truncation, f2fs rolls back to the
      // previous checkpoint, so that the orphan file can be purged normally.
      ZX_DEBUG_ASSERT(data_page->IsUptodate());
      LockedPage node_page;
      if (auto err = fs_->GetNodeManager().GetNodePage(entry->nid, &node_page); err != ZX_OK) {
        return err;
      }
      vnode->TruncateDataBlocksRange(node_page.GetPage<NodePage>(), ofs_in_node, 1);
      continue;
    }

    data_page->WaitOnWriteback();
    data_page->SetDirty();
    data_page->SetColdData();
  }

  // TODO: Instead of SyncDirtyDataPages, make a method to flush an array of locked victim Pages
  // acquired before.
  if (gc_type == GcType::kFgGc) {
    WritebackOperation op;
    op.bSync = false;
    op.if_page = [](const fbl::RefPtr<Page> &page) {
      if (page->IsColdData()) {
        return ZX_OK;
      }
      return ZX_ERR_NEXT;
    };
    fs_->SyncDirtyDataPages(op);
  }

  if (gc_type == GcType::kFgGc && fs_->GetSegmentManager().GetValidBlocks(segno, 1) != 0) {
    return ZX_ERR_BAD_STATE;
  }
  return ZX_OK;
}

}  // namespace f2fs
