// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_F2FS_SEGMENT_H_
#define SRC_STORAGE_F2FS_SEGMENT_H_

namespace f2fs {

// constant macro
constexpr uint32_t kNullSegNo = std::numeric_limits<uint32_t>::max();
constexpr uint32_t kNullSecNo = std::numeric_limits<uint32_t>::max();
constexpr uint32_t kUint32Max = std::numeric_limits<uint32_t>::max();
constexpr uint32_t kMaxSearchLimit = 4096;

// during checkpoint, BioPrivate is used to synchronize the last bio
struct BioPrivate {
  bool is_sync = false;
  void *wait = nullptr;
};

// Indicate a block allocation direction:
// kAllocRight means allocating new sections towards the end of volume.
// kAllocLeft means the opposite direction.
enum class AllocDirection {
  kAllocRight = 0,
  kAllocLeft,
};

// In the VictimSelPolicy->alloc_mode, there are two block allocation modes.
// LFS writes data sequentially with cleaning operations.
// SSR (Slack Space Recycle) reuses obsolete space without cleaning operations.
enum class AllocMode { kLFS = 0, kSSR };

// In the VictimSelPolicy->gc_mode, there are two gc, aka cleaning, modes.
// GC_CB is based on cost-benefit algorithm.
// GC_GREEDY is based on greedy algorithm.
enum class GcMode { kGcCb = 0, kGcGreedy };

// BG_GC means the background cleaning job.
// FG_GC means the on-demand cleaning job.
enum class GcType { kBgGc = 0, kFgGc };

// for a function parameter to select a victim segment
struct VictimSelPolicy {
  AllocMode alloc_mode = AllocMode::kLFS;  // LFS or SSR
  GcMode gc_mode = GcMode::kGcCb;          // cost effective or greedy
  uint8_t *dirty_segmap = nullptr;         // dirty segment bitmap
  uint32_t max_search = kMaxSearchLimit;   // maximum # of segments to search
  uint32_t offset = 0;                     // last scanned bitmap offset
  uint32_t ofs_unit = 0;                   // bitmap search unit
  uint32_t min_cost = 0;                   // minimum cost
  uint32_t min_segno = 0;                  // segment # having min. cost
};

// SSR mode uses these field to determine which blocks are allocatable.
struct SegmentEntry {
  std::unique_ptr<uint8_t[]> cur_valid_map;   // validity bitmap of blocks
  std::unique_ptr<uint8_t[]> ckpt_valid_map;  // validity bitmap in the last CP
  uint64_t mtime = 0;                         // modification time of the segment
  uint16_t valid_blocks = 0;                  // # of valid blocks
  uint16_t ckpt_valid_blocks = 0;             // # of valid blocks in the last CP
  uint8_t type = 0;                           // segment type like CURSEG_XXX_TYPE
};

struct SectionEntry {
  uint32_t valid_blocks = 0;  // # of valid blocks in a section
};

struct SitInfo {
  std::unique_ptr<uint8_t[]> sit_bitmap;             // SIT bitmap pointer
  std::unique_ptr<uint8_t[]> dirty_sentries_bitmap;  // bitmap for dirty sentries
  SegmentEntry *sentries = nullptr;                  // SIT segment-level cache
  SectionEntry *sec_entries = nullptr;               // SIT section-level cache
  block_t sit_base_addr = 0;                         // start block address of SIT area
  block_t sit_blocks = 0;                            // # of blocks used by SIT area
  block_t written_valid_blocks = 0;                  // # of valid blocks in main area
  uint32_t bitmap_size = 0;                          // SIT bitmap size
  uint32_t dirty_sentries = 0;                       // # of dirty sentries
  uint32_t sents_per_block = 0;                      // # of SIT entries per block
  // for cost-benefit algorithm in cleaning procedure
  uint64_t elapsed_time = 0;    // elapsed time after mount
  uint64_t mounted_time = 0;    // mount time
  uint64_t min_mtime = 0;       // min. modification time
  uint64_t max_mtime = 0;       // max. modification time
  fs::SharedMutex sentry_lock;  // to protect SIT cache
};

struct FreeSegmapInfo {
  std::unique_ptr<uint8_t[]> free_segmap;  // free segment bitmap
  std::unique_ptr<uint8_t[]> free_secmap;  // free section bitmap
  uint32_t start_segno = 0;                // start segment number logically
  uint32_t free_segments = 0;              // # of free segments
  uint32_t free_sections = 0;              // # of free sections
  fs::SharedMutex segmap_lock;             // free segmap lock
};

// Notice: The order of dirty type is same with CURSEG_XXX in f2fs.h
enum class DirtyType {
  kDirtyHotData = 0,  // dirty segments assigned as hot data logs
  kDirtyWarmData,     // dirty segments assigned as warm data logs
  kDirtyColdData,     // dirty segments assigned as cold data logs
  kDirtyHotNode,      // dirty segments assigned as hot node logs
  kDirtyWarmNode,     // dirty segments assigned as warm node logs
  kDirtyColdNode,     // dirty segments assigned as cold node logs
  kDirty,             // to count # of dirty segments
  kPre,               // to count # of entirely obsolete segments
  kNrDirtytype
};

struct DirtySeglistInfo {
  std::unique_ptr<uint8_t[]> dirty_segmap[static_cast<int>(DirtyType::kNrDirtytype)];
  std::mutex seglist_lock;                                       // lock for segment bitmaps
  int nr_dirty[static_cast<int>(DirtyType::kNrDirtytype)] = {};  // # of dirty segments
  std::unique_ptr<uint8_t[]> victim_secmap;                      // background gc victims
};

// for active log information
struct CursegInfo {
  union {
    SummaryBlock *sum_blk = nullptr;  // cached summary block
    FsBlock *raw_blk;
  };
  uint32_t segno = 0;        // current segment number
  uint32_t zone = 0;         // current zone number
  uint32_t next_segno = 0;   // preallocated segment
  std::mutex curseg_mutex;   // lock for consistency
  uint16_t next_blkoff = 0;  // next block offset to write
  uint8_t alloc_type = 0;    // current allocation type
};

// For SIT manager
//
// By default, there are 6 active log areas across the whole main area.
// When considering hot and cold data separation to reduce cleaning overhead,
// we split 3 for data logs and 3 for node logs as hot, warm, and cold types,
// respectively.
// In the current design, you should not change the numbers intentionally.
// Instead, as a mount option such as active_logs=x, you can use 2, 4, and 6
// logs individually according to the underlying devices. (default: 6)
// Just in case, on-disk layout covers maximum 16 logs that consist of 8 for
// data and 8 for node logs.
constexpr int kNrCursegDataType = 3;
constexpr int kNrCursegNodeType = 3;
constexpr int kNrCursegType = kNrCursegDataType + kNrCursegNodeType;

enum class CursegType {
  kCursegHotData = 0,  // directory entry blocks
  kCursegWarmData,     // data blocks
  kCursegColdData,     // multimedia or GCed data blocks
  kCursegHotNode,      // direct node blocks of directory files
  kCursegWarmNode,     // direct node blocks of normal files
  kCursegColdNode,     // indirect node blocks
  kNoCheckType
};

int LookupJournalInCursum(SummaryBlock *sum, JournalType type, uint32_t val, int alloc);

class SegmentManager {
 public:
  // Not copyable or moveable
  SegmentManager(const SegmentManager &) = delete;
  SegmentManager &operator=(const SegmentManager &) = delete;
  SegmentManager(SegmentManager &&) = delete;
  SegmentManager &operator=(SegmentManager &&) = delete;
  SegmentManager() = delete;
  SegmentManager(F2fs *fs);
  SegmentManager(SuperblockInfo *info) { superblock_info_ = info; }

  zx_status_t BuildSegmentManager();
  void DestroySegmentManager();

  SegmentEntry &GetSegmentEntry(uint32_t segno);
  SectionEntry *GetSectionEntry(uint32_t segno);
  uint32_t GetValidBlocks(uint32_t segno, uint32_t section);
  void SegInfoFromRawSit(SegmentEntry &segment_entry, SitEntry &raw_sit);
  void SegInfoToRawSit(SegmentEntry &segment_entry, SitEntry &raw_sit);
  uint32_t FindNextInuse(uint32_t max, uint32_t segno);
  void SetFree(uint32_t segno);
  void SetInuse(uint32_t segno);
  void SetTestAndFree(uint32_t segno);
  void SetTestAndInuse(uint32_t segno);
  void GetSitBitmap(void *dst_addr);
  uint32_t FreeSegments();
  uint32_t FreeSections();
  uint32_t PrefreeSegments();
  block_t DirtySegments();
  block_t OverprovisionSegments();
  block_t OverprovisionSections();
  block_t ReservedSections();
  bool NeedSSR();
  int GetSsrSegment(CursegType type);
  bool HasNotEnoughFreeSecs(uint32_t freed = 0);
  uint32_t Utilization();
  bool NeedInplaceUpdate(VnodeF2fs *vnode);
  uint32_t CursegSegno(int type);
  uint8_t CursegAllocType(int type);
  uint16_t CursegBlkoff(int type);
  void CheckSegRange(uint32_t segno) const;
  void CheckBlockCount(uint32_t segno, SitEntry &raw_sit);
  pgoff_t CurrentSitAddr(uint32_t start);
  pgoff_t NextSitAddr(pgoff_t block_addr);
  void SetToNextSit(uint32_t start);
  uint64_t GetMtime();
  void SetSummary(Summary *sum, nid_t nid, uint32_t ofs_in_node, uint8_t version);
  block_t StartSumBlock();
  block_t SumBlkAddr(int base, int type);
  bool SecUsageCheck(uint32_t secno);

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
  void GetSumPage(uint32_t segno, LockedPage *out);
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
  block_t GetWrittenBlockCount();
#if 0  // porting needed
  void VerifyBlockAddr(block_t blk_addr) = 0;
#endif
  bool HasCursegSpace(CursegType type);
  CursegType GetSegmentType2(Page &page, PageType p_type);
  CursegType GetSegmentType4(Page &page, PageType p_type);
  CursegType GetSegmentType6(Page &page, PageType p_type);
  CursegType GetSegmentType(Page &page, PageType p_type);
  zx_status_t DoWritePage(LockedPage &page, block_t old_blkaddr, block_t *new_blkaddr, Summary *sum,
                          PageType p_type);
  zx_status_t WriteMetaPage(LockedPage &page, bool is_reclaim = false);
  zx_status_t WriteNodePage(LockedPage &page, uint32_t nid, block_t old_blkaddr,
                            block_t *new_blkaddr);
  zx_status_t WriteDataPage(VnodeF2fs *vnode, LockedPage &page, nid_t nid, uint32_t ofs_in_node,
                            block_t old_blkaddr, block_t *new_blkaddr);
  zx_status_t RewriteDataPage(LockedPage &page, block_t old_blk_addr);
  void RecoverDataPage(Summary &sum, block_t old_blkaddr, block_t new_blkaddr);

  zx_status_t ReadCompactedSummaries();
  zx_status_t ReadNormalSummaries(int type);
  int RestoreCursegSummaries();
  void WriteCompactedSummaries(block_t blkaddr);
  void WriteNormalSummaries(block_t blkaddr, CursegType type);
  void WriteDataSummaries(block_t start_blk);
  void WriteNodeSummaries(block_t start_blk);

  void GetCurrentSitPage(uint32_t segno, LockedPage *out);
  void GetNextSitPage(uint32_t start, LockedPage *out);
  bool FlushSitsInJournal();
  void FlushSitEntries();

  zx_status_t BuildSitInfo();
  zx_status_t BuildFreeSegmap();
  zx_status_t BuildCurseg();
  void BuildSitEntries();
  void InitFreeSegmap();
  void InitDirtySegmap();
  zx_status_t InitVictimSecmap();
  zx_status_t BuildDirtySegmap();
  void InitMinMaxMtime();

  void DiscardDirtySegmap(enum DirtyType dirty_type);
  void DestroyVictimSecmap();
  void DestroyDirtySegmap();

  void DestroyCurseg();
  void DestroyFreeSegmap();
  void DestroySitInfo();

  block_t GetSegment0StartBlock() const { return seg0_blkaddr_; }
  block_t GetMainAreaStartBlock() const { return main_blkaddr_; }
  block_t GetSSAreaStartBlock() const { return ssa_blkaddr_; }
  block_t GetSegmentsCount() const { return segment_count_; }
  block_t GetMainSegmentsCount() const { return main_segments_; }
  block_t GetReservedSegmentsCount() const { return reserved_segments_; }
  block_t GetOPSegmentsCount() const { return ovp_segments_; }
  SitInfo &GetSitInfo() const { return *sit_info_; }
  FreeSegmapInfo &GetFreeSegmentInfo() const { return *free_info_; }
  DirtySeglistInfo &GetDirtySegmentInfo() const { return *dirty_info_; }

  void SetSegment0StartBlock(const block_t addr) { seg0_blkaddr_ = addr; }
  void SetMainAreaStartBlock(const block_t addr) { main_blkaddr_ = addr; }
  void SetSSAreaStartBlock(const block_t addr) { ssa_blkaddr_ = addr; }
  void SetSegmentsCount(const block_t count) { segment_count_ = count; }
  void SetMainSegmentsCount(const block_t count) { main_segments_ = count; }
  void SetReservedSegmentsCount(const block_t count) { reserved_segments_ = count; }
  void SetOPSegmentsCount(const block_t count) { ovp_segments_ = count; }
  void SetSitInfo(std::unique_ptr<SitInfo> &&info) { sit_info_ = std::move(info); }
  void SetFreeSegmentInfo(std::unique_ptr<FreeSegmapInfo> &&info) { free_info_ = std::move(info); }
  void SetDirtySegmentInfo(std::unique_ptr<DirtySeglistInfo> &&info) {
    dirty_info_ = std::move(info);
  }

  CursegInfo *CURSEG_I(CursegType type) { return &curseg_array_[static_cast<int>(type)]; }
  bool IsCurSeg(uint32_t segno) {
    return ((segno == CURSEG_I(CursegType::kCursegHotData)->segno) ||
            (segno == CURSEG_I(CursegType::kCursegWarmData)->segno) ||
            (segno == CURSEG_I(CursegType::kCursegColdData)->segno) ||
            (segno == CURSEG_I(CursegType::kCursegHotNode)->segno) ||
            (segno == CURSEG_I(CursegType::kCursegWarmNode)->segno) ||
            (segno == CURSEG_I(CursegType::kCursegColdNode)->segno));
  }

  bool IsCurSec(uint32_t secno) {
    return ((secno ==
             CURSEG_I(CursegType::kCursegHotData)->segno / superblock_info_->GetSegsPerSec()) ||
            (secno ==
             CURSEG_I(CursegType::kCursegWarmData)->segno / superblock_info_->GetSegsPerSec()) ||
            (secno ==
             CURSEG_I(CursegType::kCursegColdData)->segno / superblock_info_->GetSegsPerSec()) ||
            (secno ==
             CURSEG_I(CursegType::kCursegHotNode)->segno / superblock_info_->GetSegsPerSec()) ||
            (secno ==
             CURSEG_I(CursegType::kCursegWarmNode)->segno / superblock_info_->GetSegsPerSec()) ||
            (secno ==
             CURSEG_I(CursegType::kCursegColdNode)->segno / superblock_info_->GetSegsPerSec()));
  }

  // L: Logical segment number in volume, R: Relative segment number in main area
  uint32_t GetL2RSegNo(uint32_t segno) { return (segno - free_info_->start_segno); }
  uint32_t GetR2LSegNo(uint32_t segno) { return (segno + free_info_->start_segno); }
  bool IsDataSeg(CursegType t) {
    return ((t == CursegType::kCursegHotData) || (t == CursegType::kCursegColdData) ||
            (t == CursegType::kCursegWarmData));
  }
  bool IsNodeSeg(CursegType t) {
    return ((t == CursegType::kCursegHotNode) || (t == CursegType::kCursegColdNode) ||
            (t == CursegType::kCursegWarmNode));
  }
  block_t StartBlock(uint32_t segno) {
    return (seg0_blkaddr_ + (GetR2LSegNo(segno) << superblock_info_->GetLogBlocksPerSeg()));
  }
  block_t NextFreeBlkAddr(CursegType type) {
    CursegInfo *curseg = CURSEG_I(type);
    return (StartBlock(curseg->segno) + curseg->next_blkoff);
  }
  block_t GetSegOffFromSeg0(block_t blk_addr) { return blk_addr - GetSegment0StartBlock(); }
  uint32_t GetSegNoFromSeg0(block_t blk_addr) {
    return GetSegOffFromSeg0(blk_addr) >> superblock_info_->GetLogBlocksPerSeg();
  }
  uint32_t GetSegmentNumber(block_t blk_addr) {
    return ((blk_addr == kNullAddr) || (blk_addr == kNewAddr))
               ? kNullSegNo
               : GetL2RSegNo(GetSegNoFromSeg0(blk_addr));
  }
  uint32_t GetSecNo(uint32_t segno) { return segno / superblock_info_->GetSegsPerSec(); }
  uint32_t GetZoneNoFromSegNo(uint32_t segno) {
    return segno / superblock_info_->GetSegsPerSec() / superblock_info_->GetSecsPerZone();
  }
  block_t GetSumBlock(uint32_t segno) { return ssa_blkaddr_ + segno; }
  uint32_t SitEntryOffset(uint32_t segno) { return segno % sit_info_->sents_per_block; }
  block_t SitBlockOffset(uint32_t segno) { return segno / kSitEntryPerBlock; }
  block_t StartSegNo(uint32_t segno) { return SitBlockOffset(segno) * kSitEntryPerBlock; }
  uint32_t BitmapSize(uint32_t nr) {
    return static_cast<uint32_t>(BitsToLongs(nr) * sizeof(uint64_t));
  }

  block_t TotalSegs() { return main_segments_; }

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
  // If it succeeds in finding an eligible victim, it returns the segment number of the selected
  // victim. If it fails, it returns ZX_ERR_UNAVAILABLE.
  zx::result<uint32_t> GetVictimByDefault(GcType gc_type, CursegType type, AllocMode alloc_mode);

  // This function calculates the maximum cost for a victim in each GcType
  // Any segment with a less cost value becomes a victim candidate.
  uint32_t GetMaxCost(const VictimSelPolicy &policy);

  // This method determines GcMode for GetVictimByDefault
  VictimSelPolicy GetVictimSelPolicy(GcType gc_type, CursegType type, AllocMode alloc_mode);

  // This method calculates the gc cost for each dirty segment
  uint32_t GetGcCost(uint32_t segno, const VictimSelPolicy &policy);

  uint32_t GetGreedyCost(uint32_t segno);

 private:
  F2fs *fs_ = nullptr;
  SuperblockInfo *superblock_info_ = nullptr;

  std::unique_ptr<SitInfo> sit_info_;             // whole segment information
  std::unique_ptr<FreeSegmapInfo> free_info_;     // free segment information
  std::unique_ptr<DirtySeglistInfo> dirty_info_;  // dirty segment information
  CursegInfo curseg_array_[kNrCursegType];        // active segment information

  block_t seg0_blkaddr_ = 0;  // block address of 0'th segment
  block_t main_blkaddr_ = 0;  // start block address of main area
  block_t ssa_blkaddr_ = 0;   // start block address of SSA area

  block_t segment_count_ = 0;      // total # of segments
  block_t main_segments_ = 0;      // # of segments in main area
  block_t reserved_segments_ = 0;  // # of reserved segments
  block_t ovp_segments_ = 0;       // # of overprovision segments
};

}  // namespace f2fs

#endif  // SRC_STORAGE_F2FS_SEGMENT_H_
