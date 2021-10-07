// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_F2FS_F2FS_INTERNAL_H_
#define SRC_STORAGE_F2FS_F2FS_INTERNAL_H_

namespace f2fs {

class VnodeF2fs;

// For checkpoint manager
enum class MetaBitmap { kNatBitmap, kSitBitmap };

// for the list of orphan inodes
struct OrphanInodeEntry {
  list_node_t list;  // list head
  nid_t ino = 0;     // inode number
};

// for the list of directory inodes
struct DirInodeEntry {
  list_node_t list;            // list head
  VnodeF2fs *vnode = nullptr;  // vfs inode pointer
};

// for the list of fsync inodes, used only during recovery
struct FsyncInodeEntry {
  list_node_t list;                        // list head
  fbl::RefPtr<VnodeF2fs> vnode = nullptr;  // vfs inode pointer
  block_t blkaddr = 0;                     // block address locating the last inode
};

inline int NatsInCursum(SummaryBlock *sum) { return LeToCpu(sum->n_nats); }
inline int SitsInCursum(SummaryBlock *sum) { return LeToCpu(sum->n_sits); }

inline RawNatEntry NatInJournal(SummaryBlock *sum, int i) { return sum->nat_j.entries[i].ne; }
inline void SetNatInJournal(SummaryBlock *sum, int i, RawNatEntry raw_ne) {
  sum->nat_j.entries[i].ne = raw_ne;
}
inline nid_t NidInJournal(SummaryBlock *sum, int i) { return sum->nat_j.entries[i].nid; }
inline void SetNidInJournal(SummaryBlock *sum, int i, nid_t nid) {
  sum->nat_j.entries[i].nid = nid;
}

inline SitEntry *SitInJournal(SummaryBlock *sum, int i) { return &sum->sit_j.entries[i].se; }
inline uint32_t SegnoInJournal(SummaryBlock *sum, int i) { return sum->sit_j.entries[i].segno; }
inline void SetSegnoInJournal(SummaryBlock *sum, int i, uint32_t segno) {
  sum->sit_j.entries[i].segno = segno;
}

int UpdateNatsInCursum(SummaryBlock *rs, int i);

// For INODE and NODE manager
constexpr int kXattrNodeOffset = -1;
// store xattrs to one node block per
// file keeping -1 as its node offset to
// distinguish from index node blocks.
constexpr int kRdOnlyNode = 1;
// specify a read-only mode when getting
// a node block. 0 is read-write mode.
// used by GetDnodeOfData().
constexpr int kLinkMax = 32000;  // maximum link count per file

// for in-memory extent cache entry
struct ExtentInfo {
  fs::SharedMutex ext_lock;  // rwlock for consistency
  uint64_t fofs = 0;         // start offset in a file
  uint32_t blk_addr = 0;     // start block address of the extent
  uint32_t len = 0;          // lenth of the extent
};

// i_advise uses Fadvise:xxx bit. We can add additional hints later.
enum class FAdvise {
  kCold = 1,
};

struct InodeInfo {
  uint32_t i_flags = 0;          // keep an inode flags for ioctl
  uint8_t i_advise = 0;          // use to give file attribute hints
  uint8_t i_dir_level = 0;       // use for dentry level for large dir
  uint16_t i_extra_isize = 0;    // size of extra space located in i_addr
  uint64_t i_current_depth = 0;  // use only in directory structure
  umode_t i_acl_mode = 0;        // keep file acl mode temporarily

  uint32_t flags = 0;         // use to pass per-file flags
  uint64_t data_version = 0;  // lastest version of data for fsync
  atomic_t dirty_dents;       // # of dirty dentry pages
  f2fs_hash_t chash;          // hash value of given file name
  uint64_t clevel = 0;        // maximum level of given file name
  nid_t i_xattr_nid = 0;      // node id that contains xattrs
  ExtentInfo ext;             // in-memory extent cache entry
};

// this structure is used as one of function parameters.
// all the information are dedicated to a given direct node block determined
// by the data offset in a file.
struct DnodeOfData {
  // inode *inode;		// vfs inode pointer
  VnodeF2fs *vnode = nullptr;
  Page *inode_page = nullptr;      // its inode page, nullptr is possible
  Page *node_page = nullptr;       // cached direct node page
  nid_t nid = 0;                   // node id of the direct node block
  uint32_t ofs_in_node = 0;        // data offset in the node page
  bool inode_page_locked = false;  // inode page is locked or not
  block_t data_blkaddr = 0;        // block address of the node block
};

// CountType for monitoring
//
// f2fs monitors the number of several block types such as on-writeback,
// dirty dentry blocks, dirty node blocks, and dirty meta blocks.
enum class CountType {
  kWriteback = 0,
  kDirtyDents,
  kDirtyNodes,
  kDirtyMeta,
  kNrCountType,
};

// The locking order between these classes is
// LockType::FileOp -> LockType::kNodeOp
enum class LockType {
  kFileOp,  // for file op
  kNodeOp,  // for node op
  kNrLockType,
};

// The below are the page types.
// The available types are:
// kData         User data pages. It operates as async mode.
// kNode         Node pages. It operates as async mode.
// kMeta         FS metadata pages such as SIT, NAT, CP.
// kNrPageType   The number of page types.
// kMetaFlush    Make sure the previous pages are written
//               with waiting the bio's completion
// ...           Only can be used with META.
enum class PageType {
  kData = 0,
  kNode,
  kMeta,
  kNrPageType,
  kMetaFlush,
};

class SuperblockInfo {
 public:
  // Not copyable or moveable
  SuperblockInfo(const SuperblockInfo &) = delete;
  SuperblockInfo &operator=(const SuperblockInfo &) = delete;
  SuperblockInfo(SuperblockInfo &&) = delete;
  SuperblockInfo &operator=(SuperblockInfo &&) = delete;

  SuperblockInfo() = default;

  SuperBlock &GetRawSuperblock() { return *raw_superblock_; }
  void SetRawSuperblock(std::shared_ptr<SuperBlock> &raw_sb) { raw_superblock_ = raw_sb; }
  void SetRawSuperblock(SuperBlock *raw_sb_ptr) { raw_superblock_.reset(raw_sb_ptr); }

  bool IsDirty() const { return is_dirty_; }
  void SetDirty() { is_dirty_ = true; }
  void ClearDirty() { is_dirty_ = false; }

  Checkpoint &GetCheckpoint() { return checkpoint_block_.checkpoint_; }

  fbl::Mutex &GetCheckpointMutex() { return checkpoint_mutex_; };

  fs::SharedMutex &GetFsLock(LockType type) { return fs_lock_[static_cast<int>(type)]; }

  void mutex_lock_op(LockType t) __TA_ACQUIRE(&fs_lock_[static_cast<int>(t)]) {
    fs_lock_[static_cast<int>(t)].lock();
  }

  void mutex_unlock_op(LockType t) __TA_RELEASE(&fs_lock_[static_cast<int>(t)]) {
    fs_lock_[static_cast<int>(t)].unlock();
  }

  bool IsOnRecovery() const { return on_recovery_; }

  void SetOnRecovery() { on_recovery_ = true; }

  void ClearOnRecovery() { on_recovery_ = false; }

  list_node_t &GetOrphanInodeList() { return orphan_inode_list_; }

  fs::SharedMutex &GetOrphanInodeMutex() { return orphan_inode_mutex_; }

  uint64_t GetOrphanCount() const { return n_orphans_; }

  void IncNrOrphans();
  void DecNrOrphans();
  void ResetNrOrphans() { n_orphans_ = 0; }

  list_node_t &GetDirInodeList() { return dir_inode_list_; }

  fbl::Mutex &GetDirInodeLock() { return dir_inode_lock_; };

  block_t GetLogSectorsPerBlock() const { return log_sectors_per_block_; }

  void SetLogSectorsPerBlock(block_t log_sectors_per_block) {
    log_sectors_per_block_ = log_sectors_per_block;
  }

  block_t GetLogBlocksize() const { return log_blocksize_; }

  void SetLogBlocksize(block_t log_blocksize) { log_blocksize_ = log_blocksize; }

  block_t GetBlocksize() const { return blocksize_; }

  void SetBlocksize(block_t blocksize) { blocksize_ = blocksize; }

  uint32_t GetRootIno() const { return root_ino_num_; }
  void SetRootIno(uint32_t root_ino) { root_ino_num_ = root_ino; }
  uint32_t GetNodeIno() const { return node_ino_num_; }
  void SetNodeIno(uint32_t node_ino) { node_ino_num_ = node_ino; }
  uint32_t GetMetaIno() const { return meta_ino_num_; }
  void SetMetaIno(uint32_t meta_ino) { meta_ino_num_ = meta_ino; }

  block_t GetLogBlocksPerSeg() const { return log_blocks_per_seg_; }

  void SetLogBlocksPerSeg(block_t log_blocks_per_seg) { log_blocks_per_seg_ = log_blocks_per_seg; }

  block_t GetBlocksPerSeg() const { return blocks_per_seg_; }

  void SetBlocksPerSeg(block_t blocks_per_seg) { blocks_per_seg_ = blocks_per_seg; }

  block_t GetSegsPerSec() const { return segs_per_sec_; }

  void SetSegsPerSec(block_t segs_per_sec) { segs_per_sec_ = segs_per_sec; }

  block_t GetSecsPerZone() const { return secs_per_zone_; }

  void SetSecsPerZone(block_t secs_per_zone) { secs_per_zone_ = secs_per_zone; }

  block_t GetTotalSections() const { return total_sections_; }

  void SetTotalSections(block_t total_sections) { total_sections_ = total_sections; }

  nid_t GetTotalNodeCount() const { return total_node_count_; }

  void SetTotalNodeCount(nid_t total_node_count) { total_node_count_ = total_node_count; }

  nid_t GetTotalValidNodeCount() const { return total_valid_node_count_; }

  void SetTotalValidNodeCount(nid_t total_valid_node_count) {
    total_valid_node_count_ = total_valid_node_count;
  }

  nid_t GetTotalValidInodeCount() const { return total_valid_inode_count_; }

  void SetTotalValidInodeCount(nid_t total_valid_inode_count) {
    total_valid_inode_count_ = total_valid_inode_count;
  }

  int GetActiveLogs() const { return active_logs_; }

  void SetActiveLogs(int active_logs) { active_logs_ = active_logs; }

  block_t GetUserBlockCount() const { return user_block_count_; }

  void SetUserBlockCount(block_t user_block_count) { user_block_count_ = user_block_count; }

  block_t GetTotalValidBlockCount() const { return total_valid_block_count_; }

  void SetTotalValidBlockCount(block_t total_valid_block_count) {
    total_valid_block_count_ = total_valid_block_count;
  }

  block_t GetAllocValidBlockCount() const { return alloc_valid_block_count_; }

  void SetAllocValidBlockCount(block_t alloc_valid_block_count) {
    alloc_valid_block_count_ = alloc_valid_block_count;
  }

  block_t GetLastValidBlockCount() const { return last_valid_block_count_; }

  void SetLastValidBlockCount(block_t last_valid_block_count) {
    last_valid_block_count_ = last_valid_block_count;
  }

  uint32_t GetNextGeneration() const { return s_next_generation_; }

  void IncNextGeneration() { ++s_next_generation_; }

  atomic_t &GetNrPages(int type) { return nr_pages_[type]; }

  void ClearOpt(uint64_t option) { mount_opt_ &= ~option; }
  void SetOpt(uint64_t option) { mount_opt_ |= option; }
  bool TestOpt(uint64_t option) { return ((mount_opt_ & option) != 0); }

  void IncSegmentCount(int type) { ++segment_count_[type]; }

  void IncBlockCount(int type) { ++block_count_[type]; }

  uint32_t GetLastVictim(int mode) const { return last_victim_[mode]; }

  void SetLastVictim(int mode, uint32_t last_victim) { last_victim_[mode] = last_victim; }

  fbl::Mutex &GetStatLock() { return stat_lock_; };

  void IncPageCount(int count_type) {
    // TODO: IMPL
    // AtomicInc(&nr_pages_[count_type]);
    SetDirty();
  }

  void DecPageCount(CountType count_type) {
    // TODO: IMPL
    // AtomicDec(&nr_pages_[count_type]);
  }

  int GetPages(CountType count_type) const {
    // TODO: IMPL
    // return AtomicRead(&nr_pages_[count_type]);
    return 0;
  }

  uint32_t BitmapSize(MetaBitmap flag) {
    // return NAT or SIT bitmap
    if (flag == MetaBitmap::kNatBitmap)
      return LeToCpu(checkpoint_block_.checkpoint_.nat_ver_bitmap_bytesize);
    else if (flag == MetaBitmap::kSitBitmap)
      return LeToCpu(checkpoint_block_.checkpoint_.sit_ver_bitmap_bytesize);

    return 0;
  }

  void *BitmapPtr(MetaBitmap flag) {
    int offset = (flag == MetaBitmap::kNatBitmap)
                     ? checkpoint_block_.checkpoint_.sit_ver_bitmap_bytesize
                     : 0;
    return &checkpoint_block_.checkpoint_.sit_nat_version_bitmap + offset;
  }

  block_t StartCpAddr() {
    block_t start_addr;
    uint64_t ckpt_version = LeToCpu(checkpoint_block_.checkpoint_.checkpoint_ver);

    start_addr = LeToCpu(raw_superblock_->cp_blkaddr);

    // odd numbered checkpoint should at cp segment 0
    // and even segent must be at cp segment 1
    if (!(ckpt_version & 1)) {
      start_addr += blocks_per_seg_;
    }

    return start_addr;
  }

  block_t StartSumAddr() { return LeToCpu(checkpoint_block_.checkpoint_.cp_pack_start_sum); }

 private:
  std::shared_ptr<SuperBlock> raw_superblock_;  // raw super block pointer
  bool is_dirty_ = false;                       // dirty flag for checkpoint
#if 0                                           // porting needed
  // fbl::RefPtr<VnodeF2fs> node_vnode;
#endif

#if 0  // porting needed
  // struct bio *bio[static_cast<int>(PageType::kNrPageType)];             // bios to merge
  // sector_t last_block_in_bio[static_cast<int>(PageType::kNrPageType)];  // last block number
  // rw_semaphore bio_sem;		// IO semaphore

  // inode *meta_inode;		// cache meta blocks
  // fbl::RefPtr<VnodeF2fs> meta_vnode;
#endif

  union CheckpointBlock {
    Checkpoint checkpoint_;
    FsBlock fsblock_;
    CheckpointBlock() {}
  } checkpoint_block_;
  fbl::Mutex checkpoint_mutex_;                                       // for checkpoint procedure
  fs::SharedMutex fs_lock_[static_cast<int>(LockType::kNrLockType)];  // for blocking FS operations

#if 0  // porting needed
  // fbl::Mutex writepages;                                             // mutex for writepages()
#endif
  bool on_recovery_ = false;  // recovery is doing or not

  // for orphan inode management
  list_node_t orphan_inode_list_;       // orphan inode list
  fs::SharedMutex orphan_inode_mutex_;  // for orphan inode list
  uint64_t n_orphans_ = 0;              // # of orphan inodes

  // for directory inode management
  list_node_t dir_inode_list_;  // dir inode list
  fbl::Mutex dir_inode_lock_;   // for dir inode list lock
#if 0                           // porting needed
  // uint64_t n_dirty_dirs = 0;   // # of dir inodes
#endif

  // basic file system units
  block_t log_sectors_per_block_ = 0;  // log2 sectors per block
  block_t log_blocksize_ = 0;          // log2 block size
  block_t blocksize_ = 0;              // block size
  nid_t root_ino_num_ = 0;             // root inode numbe
  nid_t node_ino_num_ = 0;             // node inode numbe
  nid_t meta_ino_num_ = 0;             // meta inode numbe
  block_t log_blocks_per_seg_ = 0;     // log2 blocks per segment
  block_t blocks_per_seg_ = 0;         // blocks per segment
  block_t segs_per_sec_ = 0;           // segments per section
  block_t secs_per_zone_ = 0;          // sections per zone
  block_t total_sections_ = 0;         // total section count
  nid_t total_node_count_ = 0;         // total node block count
  nid_t total_valid_node_count_ = 0;   // valid node block count
  nid_t total_valid_inode_count_ = 0;  // valid inode count
  int active_logs_ = 0;                // # of active logs

  block_t user_block_count_ = 0;                                  // # of user blocks
  block_t total_valid_block_count_ = 0;                           // # of valid blocks
  block_t alloc_valid_block_count_ = 0;                           // # of allocated blocks
  block_t last_valid_block_count_ = 0;                            // for recovery
  uint32_t s_next_generation_ = 0;                                // for NFS support
  atomic_t nr_pages_[static_cast<int>(CountType::kNrCountType)];  // # of pages, see count_type

  uint64_t mount_opt_ = 0;  // set with kMountOptxxxx bits according to F2fs::mount_options_

  // for cleaning operations
#if 0  // porting needed
  // fs::SharedMutex gc_mutex;                    // mutex for GC
  // struct F2fsGc_kthread *gc_thread = nullptr;  // GC thread
#endif

  // for stat information.
  // one is for the LFS mode, and the other is for the SSR mode.
#if 0  // porting needed
  // struct f2fs_stat_info *stat_info = nullptr;  // FS status information
#endif
  uint64_t segment_count_[2] = {0};  // # of allocated segments
  uint64_t block_count_[2] = {0};    // # of allocated blocks
  uint32_t last_victim_[2] = {0};    // last victim segment #
#if 0                                // porting needed
  // int total_hit_ext = 0, read_hit_ext = 0;     // extent cache hit ratio
  // int bg_gc = 0;                               // background gc calls
#endif
  fbl::Mutex stat_lock_;  // lock for stat operations
};

constexpr uint32_t kDefaultAllocatedBlocks = 1;

static inline void InodeIncDirtyDents(VnodeF2fs *vnode) {
  //   //TODO: IMPL
  // 	//AtomicInc(&F2FS_I(inode)->dirty_dents);
}

static inline void InodeDecDirtyDents(void *vnode) {
  //   //TODO: IMPL
  // 	//AtomicDec(&F2FS_I(inode)->dirty_dents);
}

static inline bool IsSetCkptFlags(Checkpoint *cp, uint32_t f) {
  uint32_t ckpt_flags = LeToCpu(cp->ckpt_flags);
  return ckpt_flags & f;
}

inline void F2fsPutPage(Page *&page, int unlock) {
  if (page != nullptr) {
    delete page;
    page = nullptr;
  }
}

static inline void F2fsPutDnode(DnodeOfData *dn) {
  if (dn->inode_page == dn->node_page) {
    dn->inode_page = nullptr;
  }
  F2fsPutPage(dn->node_page, 1);
  F2fsPutPage(dn->inode_page, 0);
}

[[maybe_unused]] static inline struct kmem_cache *KmemCacheCreate(const char *name, size_t size,
                                                                  void (*ctor)(void *)) {
  return nullptr;
}

inline bool RawIsInode(Node *p) { return p->footer.nid == p->footer.ino; }

static inline bool IsInode(Page *page) {
  Node *p = static_cast<Node *>(PageAddress(page));
  return RawIsInode(p);
}

static inline uint32_t *BlkaddrInNode(Node *node) {
  if (RawIsInode(node)) {
    if (node->i.i_inline & kExtraAttr) {
      return node->i.i_addr + (node->i.i_extra_isize / sizeof(uint32_t));
    }
    return node->i.i_addr;
  }
  return node->dn.addr;
}

static inline block_t DatablockAddr(Page *node_page, uint64_t offset) {
  Node *raw_node;
  uint32_t *addr_array;
  raw_node = static_cast<Node *>(PageAddress(node_page));
  addr_array = BlkaddrInNode(raw_node);
  return LeToCpu(addr_array[offset]);
}

static inline int TestValidBitmap(uint64_t nr, uint8_t *addr) {
  int mask;

  addr += (nr >> 3);
  mask = 1 << (7 - (nr & 0x07));
  return mask & *addr;
}

static inline int SetValidBitmap(uint64_t nr, uint8_t *addr) {
  int mask;
  int ret;

  addr += (nr >> 3);
  mask = 1 << (7 - (nr & 0x07));
  ret = mask & *addr;
  *addr |= mask;
  return ret;
}

static inline int ClearValidBitmap(uint64_t nr, uint8_t *addr) {
  int mask;
  int ret;

  addr += (nr >> 3);
  mask = 1 << (7 - (nr & 0x07));
  ret = mask & *addr;
  *addr &= ~mask;
  return ret;
}

// InodeInfo->flags keeping only in memory
enum class InodeInfoFlag {
  kInit = 0,      // indicate inode is being initialized
  kActive,        // indicate open_count > 0
  kDirty,         // indicate dirty vnode
  kNewInode,      // indicate newly allocated vnode
  kNeedCp,        // need to do checkpoint during fsync
  kIncLink,       // need to increment i_nlink
  kAclMode,       // indicate acl mode
  kNoAlloc,       // should not allocate any blocks
  kUpdateDir,     // should update inode block for consistency
  kInlineXattr,   // used for inline xattr
  kInlineData,    // used for inline data
  kInlineDentry,  // used for inline dentry
  kBad,           // should drop this inode without purging
};

#if 0  // porting needed
[[maybe_unused]] static inline void SetAclInode(InodeInfo *fi, umode_t mode) {
  fi->i_acl_mode = mode;
  SetInodeFlag(fi, InodeInfoFlag::kAclMode);
}

[[maybe_unused]] static inline int CondClearInodeFlag(InodeInfo *fi, InodeInfoFlag flag) {
  if (IsInodeFlagSet(fi, InodeInfoFlag::kAclMode)) {
    ClearInodeFlag(fi, InodeInfoFlag::kAclMode);
    return 1;
  }
  return 0;
}
#endif

}  // namespace f2fs

#endif  // SRC_STORAGE_F2FS_F2FS_INTERNAL_H_
