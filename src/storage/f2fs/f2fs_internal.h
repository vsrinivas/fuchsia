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

static inline int UpdateNatsInCursum(SummaryBlock *rs, int i) {
  int before = NatsInCursum(rs);
  rs->n_nats = CpuToLe(static_cast<uint32_t>(before + i));
  return before;
}

static inline int UpdateSitsInCursum(SummaryBlock *rs, int i) {
  int before = SitsInCursum(rs);
  rs->n_sits = CpuToLe(static_cast<uint32_t>(before + i));
  return before;
}

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
  uint64_t len = 0;          // lenth of the extent
};

// i_advise uses Fadvise:xxx bit. We can add additional hints later.
enum class FAdvise {
  kCold = 1,
};

struct InodeInfo {
  uint32_t i_flags = 0;          // keep an inode flags for ioctl
  uint8_t i_advise = 0;          // use to give file attribute hints
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

struct NmInfo {
  block_t nat_blkaddr = 0;  // base disk address of NAT
  nid_t max_nid = 0;        // maximum possible node ids
  nid_t init_scan_nid = 0;  // the first nid to be scanned
  nid_t next_scan_nid = 0;  // the next nid to be scanned

  // NAT cache management
  RadixTreeRoot nat_root;         // root of the nat entry cache
  fs::SharedMutex nat_tree_lock;  // protect nat_tree_lock
  uint32_t nat_cnt = 0;           // the # of cached nat entries
  list_node_t nat_entries;        // cached nat entry list (clean)
  list_node_t dirty_nat_entries;  // cached nat entry list (dirty)

  // free node ids management
  list_node_t free_nid_list;           // a list for free nids
  fs::SharedMutex free_nid_list_lock;  // protect free nid list
  uint64_t fcnt = 0;                   // the number of free node id
  fbl::Mutex build_lock;               // lock for build free nids

  // for checkpoint
  char *nat_bitmap = nullptr;       // NAT bitmap pointer
  char *nat_prev_bitmap = nullptr;  // JY: NAT previous checkpoint bitmap pointer
  int bitmap_size = 0;              // bitmap size
};

// this structure is used as one of function parameters.
// all the information are dedicated to a given direct node block determined
// by the data offset in a file.
struct DnodeOfData {
  // inode *inode;		// vfs inode pointer
  VnodeF2fs *vnode = nullptr;
  Page *inode_page = nullptr;      // its inode page, NULL is possible
  Page *node_page = nullptr;       // cached direct node page
  nid_t nid = 0;                   // node id of the direct node block
  uint64_t ofs_in_node = 0;        // data offset in the node page
  bool inode_page_locked = false;  // inode page is locked or not
  block_t data_blkaddr = 0;        // block address of the node block
};

static inline void SetNewDnode(DnodeOfData *dn, VnodeF2fs *vnode, Page *ipage, Page *npage,
                               nid_t nid) {
  dn->vnode = vnode;
  dn->inode_page = ipage;
  dn->node_page = npage;
  dn->nid = nid;
  dn->inode_page_locked = 0;
}

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

struct SmInfo {
  struct SitInfo *SitInfo = nullptr;              // whole segment information
  struct FreeSegmapInfo *free_info = nullptr;     // free segment information
  struct DirtySeglistInfo *dirty_info = nullptr;  // dirty segment information
  struct CursegInfo *curseg_array = nullptr;      // active segment information

  block_t seg0_blkaddr = 0;  // block address of 0'th segment
  block_t main_blkaddr = 0;  // start block address of main area
  block_t ssa_blkaddr = 0;   // start block address of SSA area

  uint64_t segment_count = 0;      // total # of segments
  uint64_t main_segments = 0;      // # of segments in main area
  uint64_t reserved_segments = 0;  // # of reserved segments
  uint64_t ovp_segments = 0;       // # of overprovision segments
};

// For directory operation
constexpr size_t kNodeDir1Block = kAddrsPerInode + 1;
constexpr size_t kNodeDir2Block = kAddrsPerInode + 2;
constexpr size_t kNodeInd1Block = kAddrsPerInode + 3;
constexpr size_t kNodeInd2Block = kAddrsPerInode + 4;
constexpr size_t kNodeDIndBlock = kAddrsPerInode + 5;

// For superblock

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

struct SbInfo {
  // super_block *sb;			// pointer to VFS super block
  // buffer_head *raw_super_buf;	// buffer head of raw sb
  const SuperBlock *raw_super;  // raw super block pointer
  int s_dirty = 0;              // dirty flag for checkpoint

  // for node-related operations
  NmInfo *nm_info = nullptr;  // node manager
  // inode *node_inode;		// cache node blocks
  fbl::RefPtr<VnodeF2fs> node_vnode;

  // for segment-related operations
  SmInfo *sm_info = nullptr;                                            // segment manager
  struct bio *bio[static_cast<int>(PageType::kNrPageType)];             // bios to merge
  sector_t last_block_in_bio[static_cast<int>(PageType::kNrPageType)];  // last block number
  // rw_semaphore bio_sem;		// IO semaphore

  // for checkpoint
  Checkpoint *ckpt = nullptr;  // raw checkpoint pointer
  // inode *meta_inode;		// cache meta blocks
  fbl::RefPtr<VnodeF2fs> meta_vnode;
  fbl::Mutex cp_mutex;                                               // for checkpoint procedure
  fs::SharedMutex fs_lock[static_cast<int>(LockType::kNrLockType)];  // for blocking FS operations
  fbl::Mutex writepages;                                             // mutex for writepages()
  int por_doing = 0;                                                 // recovery is doing or not

  // for orphan inode management
  list_node_t orphan_inode_list;       // orphan inode list
  fs::SharedMutex orphan_inode_mutex;  // for orphan inode list
  uint64_t n_orphans = 0;              // # of orphan inodes

  // for directory inode management
  list_node_t dir_inode_list;  // dir inode list
  fbl::Mutex dir_inode_lock;   // for dir inode list lock
  uint64_t n_dirty_dirs = 0;   // # of dir inodes

  // basic file system units
  uint64_t log_sectors_per_block = 0;    // log2 sectors per block
  uint64_t log_blocksize = 0;            // log2 block size
  uint64_t blocksize = 0;                // block size
  uint64_t root_ino_num = 0;             // root inode numbe
  uint64_t node_ino_num = 0;             // node inode numbe
  uint64_t meta_ino_num = 0;             // meta inode numbe
  uint64_t log_blocks_per_seg = 0;       // log2 blocks per segment
  uint64_t blocks_per_seg = 0;           // blocks per segment
  uint64_t segs_per_sec = 0;             // segments per section
  uint64_t secs_per_zone = 0;            // sections per zone
  uint64_t total_sections = 0;           // total section count
  uint64_t total_node_count = 0;         // total node block count
  uint64_t total_valid_node_count = 0;   // valid node block count
  uint64_t total_valid_inode_count = 0;  // valid inode count
  int active_logs = 0;                   // # of active logs

  block_t user_block_count = 0;                                  // # of user blocks
  block_t total_valid_block_count = 0;                           // # of valid blocks
  block_t alloc_valid_block_count = 0;                           // # of allocated blocks
  block_t last_valid_block_count = 0;                            // for recovery
  uint32_t s_next_generation = 0;                                // for NFS support
  atomic_t nr_pages[static_cast<int>(CountType::kNrCountType)];  // # of pages, see count_type

  uint64_t mount_opt = 0;  // set with kMountOptxxxx bits according to F2fs::mount_options_

  // for cleaning operations
  fs::SharedMutex gc_mutex;                    // mutex for GC
  struct F2fsGc_kthread *gc_thread = nullptr;  // GC thread

  // for stat information.
  // one is for the LFS mode, and the other is for the SSR mode.
  struct f2fs_stat_info *stat_info = nullptr;  // FS status information
  uint64_t segment_count[2];                   // # of allocated segments
  uint64_t block_count[2];                     // # of allocated blocks
  uint64_t last_victim[2];                     // last victim segment #
  int total_hit_ext = 0, read_hit_ext = 0;     // extent cache hit ratio
  int bg_gc = 0;                               // background gc calls
  fbl::Mutex stat_lock;                        // lock for stat operations
};

static inline const SuperBlock *RawSuper(SbInfo *sbi) {
  return static_cast<const SuperBlock *>(sbi->raw_super);
}

static inline Checkpoint *GetCheckpoint(SbInfo *sbi) {
  return static_cast<Checkpoint *>(sbi->ckpt);
}

static inline NmInfo *GetNmInfo(SbInfo *sbi) { return static_cast<NmInfo *>(sbi->nm_info); }

static inline SmInfo *GetSmInfo(SbInfo *sbi) { return static_cast<SmInfo *>(sbi->sm_info); }

static inline SitInfo *GetSitInfo(SbInfo *sbi) {
  return static_cast<SitInfo *>((GetSmInfo(sbi)->SitInfo));
}

static inline FreeSegmapInfo *GetFreeInfo(SbInfo *sbi) {
  return static_cast<FreeSegmapInfo *>(GetSmInfo(sbi)->free_info);
}

static inline DirtySeglistInfo *GetDirtyInfo(SbInfo *sbi) {
  return static_cast<DirtySeglistInfo *>(GetSmInfo(sbi)->dirty_info);
}

static inline void SetSbDirt(SbInfo *sbi) { sbi->s_dirty = 1; }

static inline void ResetSbDirt(SbInfo *sbi) { sbi->s_dirty = 0; }

static inline void mutex_lock_op(SbInfo *sbi, LockType t)
    TA_ACQ(&sbi->fs_lock[static_cast<int>(t)]) {
  sbi->fs_lock[static_cast<int>(t)].lock();
}

static inline void mutex_unlock_op(SbInfo *sbi, LockType t)
    TA_REL(&sbi->fs_lock[static_cast<int>(t)]) {
  sbi->fs_lock[static_cast<int>(t)].unlock();
}

constexpr uint32_t kDefaultAllocatedBlocks = 1;

[[maybe_unused]] static inline void IncPageCount(SbInfo *sbi, int count_type) {
  // TODO: IMPL
  // AtomicInc(&sbi->nr_pages[count_type]);
  SetSbDirt(sbi);
}

static inline void InodeIncDirtyDents(VnodeF2fs *vnode) {
  //   //TODO: IMPL
  // 	//AtomicInc(&F2FS_I(inode)->dirty_dents);
}

static inline void DecPageCount(SbInfo *sbi, CountType count_type) {
  // TODO: IMPL
  // AtomicDec(&sbi->nr_pages[count_type]);
}

static inline void InodeDecDirtyDents(void *vnode) {
  //   //TODO: IMPL
  // 	//AtomicDec(&F2FS_I(inode)->dirty_dents);
}

static inline int GetPages(SbInfo *sbi, CountType count_type) {
  // TODO: IMPL
  // return AtomicRead(&sbi->nr_pages[count_type]);
  return 0;
}

static inline uint32_t BitmapSize(SbInfo *sbi, MetaBitmap flag) {
  Checkpoint *ckpt = GetCheckpoint(sbi);

  // return NAT or SIT bitmap
  if (flag == MetaBitmap::kNatBitmap)
    return LeToCpu(ckpt->nat_ver_bitmap_bytesize);
  else if (flag == MetaBitmap::kSitBitmap)
    return LeToCpu(ckpt->sit_ver_bitmap_bytesize);

  return 0;
}

static inline void *BitmapPrt(SbInfo *sbi, MetaBitmap flag) {
  Checkpoint *ckpt = GetCheckpoint(sbi);
  int offset = (flag == MetaBitmap::kNatBitmap) ? ckpt->sit_ver_bitmap_bytesize : 0;
  return &ckpt->sit_nat_version_bitmap + offset;
}

static inline bool IsSetCkptFlags(Checkpoint *cp, uint32_t f) {
  uint32_t ckpt_flags = LeToCpu(cp->ckpt_flags);
  return ckpt_flags & f;
}

static inline block_t StartCpAddr(SbInfo *sbi) {
  block_t start_addr;
  Checkpoint *ckpt = GetCheckpoint(sbi);
  uint64_t ckpt_version = LeToCpu(ckpt->checkpoint_ver);

  start_addr = LeToCpu(RawSuper(sbi)->cp_blkaddr);

  // odd numbered checkpoint should at cp segment 0
  // and even segent must be at cp segment 1
  if (!(ckpt_version & 1))
    start_addr += sbi->blocks_per_seg;

  return start_addr;
}

[[maybe_unused]] static inline block_t StartSumAddr(SbInfo *sbi) {
  return LeToCpu(GetCheckpoint(sbi)->cp_pack_start_sum);
}

inline void F2fsPutPage(Page *page, int unlock) {
  if (page != nullptr)
    delete page;
}

static inline void F2fsPutDnode(DnodeOfData *dn) {
  // TODO: IMPL
  if (dn->node_page)
    F2fsPutPage(dn->node_page, 1);
  if (dn->inode_page && dn->node_page != dn->inode_page)
    F2fsPutPage(dn->inode_page, 0);
  dn->node_page = NULL;
  dn->inode_page = NULL;
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
  return RawIsInode(node) ? node->i.i_addr : node->dn.addr;
}

static inline block_t DatablockAddr(Page *node_page, uint64_t offset) {
  Node *raw_node;
  uint32_t *addr_array;
  raw_node = static_cast<Node *>(PageAddress(node_page));
  addr_array = BlkaddrInNode(raw_node);
  return LeToCpu(addr_array[offset]);
}

static inline int TestValidBitmap(uint64_t nr, char *addr) {
  int mask;

  addr += (nr >> 3);
  mask = 1 << (7 - (nr & 0x07));
  return mask & *addr;
}

static inline int SetValidBitmap(uint64_t nr, char *addr) {
  int mask;
  int ret;

  addr += (nr >> 3);
  mask = 1 << (7 - (nr & 0x07));
  ret = mask & *addr;
  *addr |= mask;
  return ret;
}

static inline int ClearValidBitmap(uint64_t nr, char *addr) {
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

inline uint32_t RootIno(SbInfo *sbi) { return sbi->root_ino_num; }
inline uint32_t NodeIno(SbInfo *sbi) { return sbi->node_ino_num; }
inline uint32_t MetaIno(SbInfo *sbi) { return sbi->meta_ino_num; }

}  // namespace f2fs

#endif  // SRC_STORAGE_F2FS_F2FS_INTERNAL_H_
