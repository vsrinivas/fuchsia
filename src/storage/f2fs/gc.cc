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

zx::status<uint32_t> SegmentManager::GetVictimByDefault(GcType gc_type, CursegType type,
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

  if (policy.min_segno == kNullSegNo) {
    block_t last_segment = TotalSegs();
    while (nSearched < policy.max_search) {
      uint32_t segno = FindNextBit(policy.dirty_segmap, last_segment, policy.offset);
      if (segno >= last_segment) {
        if (superblock_info_->GetLastVictim(static_cast<int>(policy.gc_mode))) {
          last_segment = superblock_info_->GetLastVictim(static_cast<int>(policy.gc_mode));
          superblock_info_->SetLastVictim(static_cast<int>(policy.gc_mode), 0);
          policy.offset = 0;
          continue;
        }
        break;
      }
      policy.offset = segno + policy.ofs_unit;
      uint32_t secno = GetSecNo(segno);

      if (policy.ofs_unit > 1) {
        policy.offset -= segno % policy.ofs_unit;
        nSearched +=
            CountBits(policy.dirty_segmap, policy.offset - policy.ofs_unit, policy.ofs_unit);
      } else {
        ++nSearched;
      }

      // TODO: Remove it when data gc is implemented.
      if (policy.alloc_mode == AllocMode::kLFS &&
          IsDataSeg(static_cast<CursegType>(GetSegmentEntry(segno).type))) {
        continue;
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
        superblock_info_->SetLastVictim(
            static_cast<int>(policy.gc_mode),
            (segno + 1) % fs_->GetSegmentManager().GetMainSegmentsCount());
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

zx::status<uint32_t> GcManager::GetGcVictim(GcType gc_type, CursegType type) {
  SitInfo &sit_i = fs_->GetSegmentManager().GetSitInfo();
  std::lock_guard sentry_lock(sit_i.sentry_lock);
  return fs_->GetSegmentManager().GetVictimByDefault(gc_type, type, AllocMode::kLFS);
}

zx::status<uint32_t> GcManager::F2fsGc() {
  // For testing
  if (disable_gc_for_test_) {
    return zx::ok(0);
  }

  std::lock_guard gc_lock(gc_mutex_);

  // TODO: Default gc_type should be kBgGc when background gc is implemented.
  GcType gc_type = GcType::kFgGc;
  uint32_t sec_freed = 0;

  while (true) {
    if (fs_->GetSuperblockInfo().TestCpFlags(CpFlag::kCpErrorFlag)) {
      return zx::error(ZX_ERR_BAD_STATE);
    }

    if (gc_type == GcType::kBgGc && fs_->GetSegmentManager().HasNotEnoughFreeSecs()) {
      // For example, if there are many prefree_segments below given threshold, we can make them
      // free by checkpoint. Then, we secure free segments which doesn't need fggc any more.
      if (fs_->GetSegmentManager().PrefreeSegments()) {
        fs_->WriteCheckpoint(false, false);
      }
      if (fs_->GetSegmentManager().HasNotEnoughFreeSecs()) {
        gc_type = GcType::kFgGc;
      }
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

    if (!fs_->GetSegmentManager().HasNotEnoughFreeSecs()) {
      break;
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

    SummaryBlock *sum = sum_page->GetAddress<SummaryBlock>();
    ZX_DEBUG_ASSERT(type == GetSumType((&sum->footer)));

    if (type == kSumTypeNode) {
      if (auto err = GcNodeSegment(sum, segno, gc_type); err != ZX_OK) {
        return err;
      }
    } else {
      // TODO: Add GcDataSegment()
      return ZX_ERR_NOT_SUPPORTED;
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

zx_status_t GcManager::GcNodeSegment(const SummaryBlock *sum, uint32_t segno, GcType gc_type) {
  const Summary *entry = sum->entries;
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

}  // namespace f2fs
