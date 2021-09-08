// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_F2FS_SEGMENT_H_
#define SRC_STORAGE_F2FS_SEGMENT_H_

namespace f2fs {

/* constant macro */
constexpr uint32_t kNullSegNo = std::numeric_limits<uint32_t>::max();
constexpr uint32_t kUint32Max = std::numeric_limits<uint32_t>::max();
constexpr uint32_t kMaxSearchLimit = 20;

/* during checkpoint, BioPrivate is used to synchronize the last bio */
struct BioPrivate {
  bool is_sync = false;
  void *wait = nullptr;
};

/*
 * indicate a block allocation direction: RIGHT and LEFT.
 * RIGHT means allocating new sections towards the end of volume.
 * LEFT means the opposite direction.
 */
enum class AllocDirection {
  kAllocRight = 0,
  kAllocLeft,
};

/*
 * In the VictimSelPolicy->alloc_mode, there are two block allocation modes.
 * LFS writes data sequentially with cleaning operations.
 * SSR (Slack Space Recycle) reuses obsolete space without cleaning operations.
 */
enum class AllocMode { kLFS = 0, kSSR };

/*
 * In the VictimSelPolicy->gc_mode, there are two gc, aka cleaning, modes.
 * GC_CB is based on cost-benefit algorithm.
 * GC_GREEDY is based on greedy algorithm.
 */
enum class GcMode { kGcCb = 0, kGcGreedy };

/*
 * BG_GC means the background cleaning job.
 * FG_GC means the on-demand cleaning job.
 */
enum class GcType { kBgGc = 0, kFgGc };

// for a function parameter to select a victim segment
struct VictimSelPolicy {
  AllocMode alloc_mode = AllocMode::kLFS;  // LFS or SSR
  GcMode gc_mode = GcMode::kGcCb;          // cost effective or greedy
  uint64_t *dirty_segmap = nullptr;        // dirty segment bitmap
  uint32_t offset = 0;                     // last scanned bitmap offset
  uint32_t ofs_unit = 0;                   // bitmap search unit
  uint32_t min_cost = 0;                   // minimum cost
  uint32_t min_segno = 0;                  // segment # having min. cost
};

struct SegEntry {
  uint16_t valid_blocks = 0;        /* # of valid blocks */
  uint8_t *cur_valid_map = nullptr; /* validity bitmap of blocks */
  /*
   * # of valid blocks and the validity bitmap stored in the the last
   * checkpoint pack. This information is used by the SSR mode.
   */
  uint16_t ckpt_valid_blocks = 0;
  uint8_t *ckpt_valid_map = nullptr;
  uint8_t type = 0;   /* segment type like CURSEG_XXX_TYPE */
  uint64_t mtime = 0; /* modification time of the segment */
};

struct SecEntry {
  uint32_t valid_blocks = 0; /* # of valid blocks in a section */
};

struct SegmentAllocation {
  void (*allocate_segment)(SbInfo *, int, bool) = nullptr;
};

struct SitInfo {
  const SegmentAllocation *s_ops = nullptr;

  block_t sit_base_addr = 0;        /* start block address of SIT area */
  block_t sit_blocks = 0;           /* # of blocks used by SIT area */
  block_t written_valid_blocks = 0; /* # of valid blocks in main area */
  char *sit_bitmap = nullptr;       /* SIT bitmap pointer */
  uint32_t bitmap_size = 0;         /* SIT bitmap size */

  uint64_t *dirty_sentries_bitmap = nullptr; /* bitmap for dirty sentries */
  uint32_t dirty_sentries = 0;               /* # of dirty sentries */
  uint32_t sents_per_block = 0;              /* # of SIT entries per block */
  fbl::Mutex sentry_lock;                    /* to protect SIT cache */
  SegEntry *sentries = nullptr;              /* SIT segment-level cache */
  SecEntry *sec_entries = nullptr;           /* SIT section-level cache */

  /* for cost-benefit algorithm in cleaning procedure */
  uint64_t elapsed_time = 0; /* elapsed time after mount */
  uint64_t mounted_time = 0; /* mount time */
  uint64_t min_mtime = 0;    /* min. modification time */
  uint64_t max_mtime = 0;    /* max. modification time */
};

struct FreeSegmapInfo {
  uint32_t start_segno = 0;        /* start segment number logically */
  uint32_t free_segments = 0;      /* # of free segments */
  uint32_t free_sections = 0;      /* # of free sections */
  fs::SharedMutex segmap_lock;     /* free segmap lock */
  uint64_t *free_segmap = nullptr; /* free segment bitmap */
  uint64_t *free_secmap = nullptr; /* free section bitmap */
};

/* Notice: The order of dirty type is same with CURSEG_XXX in f2fs.h */
enum class DirtyType {
  kDirtyHotData = 0, /* dirty segments assigned as hot data logs */
  kDirtyWarmData,    /* dirty segments assigned as warm data logs */
  kDirtyColdData,    /* dirty segments assigned as cold data logs */
  kDirtyHotNode,     /* dirty segments assigned as hot node logs */
  kDirtyWarmNode,    /* dirty segments assigned as warm node logs */
  kDirtyColdNode,    /* dirty segments assigned as cold node logs */
  kDirty,            /* to count # of dirty segments */
  kPre,              /* to count # of entirely obsolete segments */
  kNrDirtytype
};

/* victim selection function for cleaning and SSR */
struct VictimSelection {
  int (*get_victim)(SbInfo *, uint32_t *, int, int, char) = nullptr;
};

struct DirtySeglistInfo {
  const VictimSelection *v_ops = nullptr; /* victim selction operation */
  uint64_t *dirty_segmap[static_cast<int>(DirtyType::kNrDirtytype)] = {};
  fbl::Mutex seglist_lock;                                      /* lock for segment bitmaps */
  int nr_dirty[static_cast<int>(DirtyType::kNrDirtytype)] = {}; /* # of dirty segments */
  uint64_t *victim_segmap[2] = {};                              /* BG_GC, FG_GC */
};

/* for active log information */
struct CursegInfo {
  fbl::Mutex curseg_mutex;         /* lock for consistency */
  SummaryBlock *sum_blk = nullptr; /* cached summary block */
  uint8_t alloc_type = 0;          /* current allocation type */
  uint32_t segno = 0;              /* current segment number */
  uint16_t next_blkoff = 0;        /* next block offset to write */
  uint32_t zone = 0;               /* current zone number */
  uint32_t next_segno = 0;         /* preallocated segment */
};

/* V: Logical segment # in volume, R: Relative segment # in main area */
inline uint32_t GetL2RSegNo(FreeSegmapInfo *free_i, uint32_t segno) {
  return (segno - free_i->start_segno);
}
inline uint32_t GetR2LSegNo(FreeSegmapInfo *free_i, uint32_t segno) {
  return (segno + free_i->start_segno);
}

inline uint32_t IsDataSeg(CursegType t) {
  return ((t == CursegType::kCursegHotData) || (t == CursegType::kCursegColdData) ||
          (t == CursegType::kCursegWarmData));
}

inline uint32_t IsNodeSeg(CursegType t) {
  return ((t == CursegType::kCursegHotNode) || (t == CursegType::kCursegColdNode) ||
          (t == CursegType::kCursegWarmNode));
}

inline block_t StartBlock(SbInfo *sbi, uint32_t segno) {
  return (GetSmInfo(sbi)->seg0_blkaddr +
          (GetR2LSegNo(GetFreeInfo(sbi), segno) << (sbi)->log_blocks_per_seg));
}
inline block_t NextFreeBlkAddr(SbInfo *sbi, CursegInfo *curseg) {
  return (StartBlock(sbi, curseg->segno) + curseg->next_blkoff);
}

inline block_t MainBaseBlock(SbInfo *sbi) { return GetSmInfo(sbi)->main_blkaddr; }

inline block_t GetSegOffFromSeg0(SbInfo *sbi, block_t blk_addr) {
  return blk_addr - GetSmInfo(sbi)->seg0_blkaddr;
}
inline uint32_t GetSegNoFromSeg0(SbInfo *sbi, block_t blk_addr) {
  return GetSegOffFromSeg0(sbi, blk_addr) >> sbi->log_blocks_per_seg;
}

inline uint32_t GetSegNo(SbInfo *sbi, block_t blk_addr) {
  return ((blk_addr == kNullAddr) || (blk_addr == kNewAddr))
             ? kNullSegNo
             : GetL2RSegNo(GetFreeInfo(sbi), GetSegNoFromSeg0(sbi, blk_addr));
}

inline uint32_t GetSecNo(SbInfo *sbi, uint32_t segno) { return segno / sbi->segs_per_sec; }

inline uint32_t GetZoneNoFromSegNo(SbInfo *sbi, uint32_t segno) {
  return (segno / sbi->segs_per_sec) / sbi->secs_per_zone;
}

inline block_t GetSumBlock(SbInfo *sbi, uint32_t segno) {
  return (sbi->sm_info->ssa_blkaddr) + segno;
}

inline uint32_t SitEntryOffset(SitInfo *sit_i, uint32_t segno) {
  return segno % sit_i->sents_per_block;
}
inline uint32_t SitBlockOffset(SitInfo *sit_i, uint32_t segno) { return segno / kSitEntryPerBlock; }
inline uint32_t StartSegNo(SitInfo *sit_i, uint32_t segno) {
  return SitBlockOffset(sit_i, segno) * kSitEntryPerBlock;
}
inline uint32_t BitmapSize(uint32_t nr) {
  return static_cast<uint32_t>(BitsToLongs(nr) * sizeof(uint64_t));
}
inline block_t TotalSegs(SbInfo *sbi) { return GetSmInfo(sbi)->main_segments; }

class SegMgr {
 public:
  // Not copyable or moveable
  SegMgr(const SegMgr &) = delete;
  SegMgr &operator=(const SegMgr &) = delete;
  SegMgr(SegMgr &&) = delete;
  SegMgr &operator=(SegMgr &&) = delete;

  // TODO: Implement constructor
  SegMgr(F2fs *fs);

  // TODO: Implement destructor
  ~SegMgr() = default;

  // Static functions
  static CursegInfo *CURSEG_I(SbInfo *sbi, CursegType type);
  static int LookupJournalInCursum(SummaryBlock *sum, JournalType type, uint32_t val, int alloc);

  // Public functions
  zx_status_t BuildSegmentManager();
  void DestroySegmentManager();
  void RewriteNodePage(Page *page, Summary *sum, block_t old_blkaddr, block_t new_blkaddr);

 private:
  // GetVictimByDefault() is called for two purposes:
  // 1) One is to select a victim segment for garbage collection, and
  // 2) the other is to find a dirty segment used for SSR.
  // For GC, it tries to find a victim segment that might require less cost
  // to secure free segments among all types of dirty segments.
  // The gc cost can be calucalted in two ways according to GcType.
  // In case of GcType::kFgGc, it is typically triggered in the middle of user IO path,
  // and thus it selects a victim with a less valid block count (i.e., GcMode::kGcGreedy)
  // as it hopes the migration completes more quickly.
  // In case of GcType::kBgGc, it is triggered at a idle time,
  // so it uses a cost-benefit method (i.e., GcMode:: kGcCb) rather than kGcGreedy for the victim
  // selection. kGcCb tries to find a cold segment as a victim as it hopes to mitigate a block
  // thrashing problem.
  // Meanwhile, SSR is to reuse invalid blocks for new block allocation, and thus
  // it uses kGcGreedy to select a dirty segment with more invalid blocks
  // among the same type of dirty segments as that of the current segment.
  // |out| contains the segment number of the seleceted victim, and
  // it returns true when it finds a victim segment.
  bool GetVictimByDefault(GcType gc_type, CursegType type, AllocMode alloc_mode, uint32_t *out);

  // This function calculates the maximum cost for a victim in each GcType
  // Any segment with a less cost value becomes a victim candidate.
  uint32_t GetMaxCost(VictimSelPolicy *p);

  // This method determines GcMode for GetVictimByDefault
  void SelectPolicy(GcType gc_type, CursegType type, VictimSelPolicy *p);

  // This method calculates the gc cost for each dirty segment
  uint32_t GetGcCost(uint32_t segno, VictimSelPolicy *p);

 private:
  F2fs *fs_;

 public:
  // Inline functions
  SegEntry *GetSegEntry(uint32_t segno);
  SecEntry *GetSecEntry(uint32_t segno);
  uint32_t GetValidBlocks(uint32_t segno, int section);
  void SegInfoFromRawSit(SegEntry *se, SitEntry *rs);
  void SegInfoToRawSit(SegEntry *se, SitEntry *rs);
  uint32_t FindNextInuse(FreeSegmapInfo *free_i, uint32_t max, uint32_t segno);
  void SetFree(uint32_t segno);
  void SetInuse(uint32_t segno);
  void SetTestAndFree(uint32_t segno);
  void SetTestAndInuse(uint32_t segno);
  void GetSitBitmap(void *dst_addr);
#if 0  // porting needed
  block_t WrittenBlockCount();
#endif
  uint32_t FreeSegments();
  int ReservedSegments();
  uint32_t FreeSections();
  uint32_t PrefreeSegments();
  uint32_t DirtySegments();
  int OverprovisionSegments();
  int OverprovisionSections();
  int ReservedSections();
  bool NeedSSR();
  int GetSsrSegment(CursegType type);
  bool HasNotEnoughFreeSecs();
  uint32_t Utilization();
  bool NeedInplaceUpdate(VnodeF2fs *vnode);
  uint32_t CursegSegno(int type);
  uint8_t CursegAllocType(int type);
  uint16_t CursegBlkoff(int type);
  void CheckSegRange(uint32_t segno);
#if 0  // porting needed
  void VerifyBlockAddr(block_t blk_addr);
#endif
  void CheckBlockCount(int segno, SitEntry *raw_sit);
  pgoff_t CurrentSitAddr(uint32_t start);
  pgoff_t NextSitAddr(pgoff_t block_addr);
  void SetToNextSit(SitInfo *sit_i, uint32_t start);
  uint64_t GetMtime();
  void SetSummary(Summary *sum, nid_t nid, uint32_t ofs_in_node, uint8_t version);
  block_t StartSumBlock();
  block_t SumBlkAddr(int base, int type);

  // Functions
  int NeedToFlush();
  void BalanceFs();
  void LocateDirtySegment(uint32_t segno, enum DirtyType dirty_type);
  void RemoveDirtySegment(uint32_t segno, enum DirtyType dirty_type);
  void LocateDirtySegment(uint32_t segno);
  void SetPrefreeAsFreeSegments();
  void ClearPrefreeSegments();
  void MarkSitEntryDirty(uint32_t segno);
  void SetSitEntryType(CursegType type, uint32_t segno, int modified);
  void UpdateSitEntry(block_t blkaddr, int del);
  void RefreshSitEntry(block_t old_blkaddr, block_t new_blkaddr);
  void InvalidateBlocks(block_t addr);
  void AddSumEntry(CursegType type, Summary *sum, uint16_t offset);
  int NpagesForSummaryFlush();
  Page *GetSumPage(uint32_t segno);
  void WriteSumPage(SummaryBlock *sum_blk, block_t blk_addr);
  uint32_t CheckPrefreeSegments(int ofs_unit, CursegType type);
  void GetNewSegment(uint32_t *newseg, bool new_sec, int dir);
  void ResetCurseg(CursegType type, int modified);
  void NewCurseg(CursegType type, bool new_sec);
  void NextFreeBlkoff(CursegInfo *seg, block_t start);
  void RefreshNextBlkoff(CursegInfo *seg);
  void ChangeCurseg(CursegType type, bool reuse);
  void AllocateSegmentByDefault(CursegType type, bool force);
  void AllocateNewSegments();

#if 0  // porting needed
  //	const struct SegmentAllocation default_salloc_ops = {
  //		.allocate_segment = AllocateSegmentByDefault,
  //	};
#endif

#if 0  // porting needed
  void EndIoWrite(bio *bio, int err);
  bio *BioAlloc(block_device *bdev, sector_t first_sector, int nr_vecs,
                           gfp_t gfp_flags);
  void DoSubmitBio(PageType type, bool sync);
#endif
  void SubmitBio(PageType type, bool sync);
  void SubmitWritePage(Page *page, block_t blk_addr, PageType type);
  bool HasCursegSpace(CursegType type);
  CursegType GetSegmentType2(Page *page, PageType p_type);
  CursegType GetSegmentType4(Page *page, PageType p_type);
  CursegType GetSegmentType6(Page *page, PageType p_type);
  CursegType GetSegmentType(Page *page, PageType p_type);
  void DoWritePage(Page *page, block_t old_blkaddr, block_t *new_blkaddr, Summary *sum,
                   PageType p_type);
  zx_status_t WriteMetaPage(Page *page, WritebackControl *wbc);
  void WriteNodePage(Page *page, uint32_t nid, block_t old_blkaddr, block_t *new_blkaddr);
  void WriteDataPage(VnodeF2fs *vnode, Page *page, DnodeOfData *dn, block_t old_blkaddr,
                     block_t *new_blkaddr);
  void RewriteDataPage(Page *page, block_t old_blk_addr);
  void RecoverDataPage(Page *page, Summary *sum, block_t old_blkaddr, block_t new_blkaddr);

  int ReadCompactedSummaries();
  int ReadNormalSummaries(int type);
  int RestoreCursegSummaries();
  void WriteCompactedSummaries(block_t blkaddr);
  void WriteNormalSummaries(block_t blkaddr, CursegType type);
  void WriteDataSummaries(block_t start_blk);
  void WriteNodeSummaries(block_t start_blk);

  Page *GetCurrentSitPage(uint32_t segno);
  Page *GetNextSitPage(uint32_t start);
  bool FlushSitsInJournal();
  void FlushSitEntries();

  //////////////////////////////////////////// BUILD
  ///////////////////////////////////////////////////////////

  zx_status_t BuildSitInfo();
  zx_status_t BuildFreeSegmap();
  zx_status_t BuildCurseg();
  void BuildSitEntries();
  void InitFreeSegmap();
  void InitDirtySegmap();
  zx_status_t InitVictimSegmap();
  zx_status_t BuildDirtySegmap();
  void InitMinMaxMtime();

  void DiscardDirtySegmap(enum DirtyType dirty_type);
  void ResetVictimSegmap();
  void DestroyVictimSegmap();
  void DestroyDirtySegmap();

  void DestroyCurseg();
  void DestroyFreeSegmap();
  void DestroySitInfo();
};

inline CursegInfo *SegMgr::CURSEG_I(SbInfo *sbi, CursegType type) {
  return (CursegInfo *)(GetSmInfo(sbi)->curseg_array + static_cast<int>(type));
}

inline bool IsCurSeg(SbInfo *sbi, uint32_t segno) {
  return ((segno == SegMgr::CURSEG_I(sbi, CursegType::kCursegHotData)->segno) ||
          (segno == SegMgr::CURSEG_I(sbi, CursegType::kCursegWarmData)->segno) ||
          (segno == SegMgr::CURSEG_I(sbi, CursegType::kCursegColdData)->segno) ||
          (segno == SegMgr::CURSEG_I(sbi, CursegType::kCursegHotNode)->segno) ||
          (segno == SegMgr::CURSEG_I(sbi, CursegType::kCursegWarmNode)->segno) ||
          (segno == SegMgr::CURSEG_I(sbi, CursegType::kCursegColdNode)->segno));
}

inline bool IsCurSec(SbInfo *sbi, uint32_t secno) {
  return (
      (secno == SegMgr::CURSEG_I(sbi, CursegType::kCursegHotData)->segno / (sbi)->segs_per_sec) ||
      (secno == SegMgr::CURSEG_I(sbi, CursegType::kCursegWarmData)->segno / (sbi)->segs_per_sec) ||
      (secno == SegMgr::CURSEG_I(sbi, CursegType::kCursegColdData)->segno / (sbi)->segs_per_sec) ||
      (secno == SegMgr::CURSEG_I(sbi, CursegType::kCursegHotNode)->segno / (sbi)->segs_per_sec) ||
      (secno == SegMgr::CURSEG_I(sbi, CursegType::kCursegWarmNode)->segno / (sbi)->segs_per_sec) ||
      (secno == SegMgr::CURSEG_I(sbi, CursegType::kCursegColdNode)->segno / (sbi)->segs_per_sec));
}

}  // namespace f2fs

#endif  // SRC_STORAGE_F2FS_SEGMENT_H_
