// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_F2FS_NODE_H_
#define SRC_STORAGE_F2FS_NODE_H_

namespace f2fs {

// start node id of a node block dedicated to the given node id
inline uint32_t StartNid(uint32_t nid) { return (nid / kNatEntryPerBlock) * kNatEntryPerBlock; }

// node block offset on the NAT area dedicated to the given start node id
inline uint64_t NatBlockOffset(uint32_t start_nid) { return start_nid / kNatEntryPerBlock; }

// # of pages to perform readahead before building free nids
constexpr int kFreeNidPages = 4;

// maximum # of free node ids to produce during build_free_nids
constexpr int kMaxFreeNids = kNatEntryPerBlock * kFreeNidPages;

// maximum readahead size for node during getting data blocks
constexpr int kMaxRaNode = 128;

// maximum cached nat entries to manage memory footprint
constexpr uint32_t kNmWoutThreshold = 64 * kNatEntryPerBlock;

// vector size for gang look-up from nat cache that consists of radix tree
constexpr uint32_t kNatvecSize = 64;

// For directory operation
constexpr size_t kNodeDir1Block = kAddrsPerInode + 1;
constexpr size_t kNodeDir2Block = kAddrsPerInode + 2;
constexpr size_t kNodeInd1Block = kAddrsPerInode + 3;
constexpr size_t kNodeInd2Block = kAddrsPerInode + 4;
constexpr size_t kNodeDIndBlock = kAddrsPerInode + 5;

// maximum node block level of data block
constexpr uint32_t kMaxNodeBlockLevel = 4;

// For node information
struct NodeInfoDeprecated {
  nid_t nid = 0;         // node id
  nid_t ino = 0;         // inode number of the node's owner
  block_t blk_addr = 0;  // block address of the node
  uint8_t version = 0;   // version of the node
};

class NatEntry : public fbl::WAVLTreeContainable<std::unique_ptr<NatEntry>>,
                 public fbl::DoublyLinkedListable<NatEntry *> {
 public:
  NatEntry() = default;
  NatEntry(const NatEntry &) = delete;
  NatEntry &operator=(const NatEntry &) = delete;
  NatEntry(const NatEntry &&) = delete;
  NatEntry &operator=(const NatEntry &&) = delete;

  const NodeInfoDeprecated &GetNodeInfo() { return ni_; }
  void SetNodeInfo(const NodeInfoDeprecated &value) { ni_ = value; }

  bool IsCheckpointed() const { return checkpointed_; }
  void SetCheckpointed() { checkpointed_ = true; }
  void ClearCheckpointed() { checkpointed_ = false; }
  uint32_t GetNid() const { return ni_.nid; }
  void SetNid(const nid_t value) { ni_.nid = value; }
  block_t GetBlockAddress() { return ni_.blk_addr; }
  void SetBlockAddress(const block_t value) { ni_.blk_addr = value; }
  uint32_t GetIno() { return ni_.ino; }
  void SetIno(const nid_t value) { ni_.ino = value; }
  uint8_t GetVersion() { return ni_.version; }
  void SetVersion(const uint8_t value) { ni_.version = value; }
  ino_t GetKey() const { return ni_.nid; }

 private:
  bool checkpointed_ = false;  // whether it is checkpointed or not
  NodeInfoDeprecated ni_;      // in-memory node information
};

inline uint8_t IncNodeVersion(uint8_t version) { return ++version; }

// For free nid mangement
enum class NidState {
  kNidNew = 0,  // newly added to free nid list
  kNidAlloc,    // it is allocated
};

struct FreeNid {
  list_node_t list;  // for free node id list
  nid_t nid = 0;     // node id
  int state = 0;     // in use or not: kNidNew or kNidAlloc
};

class MapTester;

class NodeManager {
 public:
  // Not copyable or moveable
  NodeManager(const NodeManager &) = delete;
  NodeManager &operator=(const NodeManager &) = delete;
  NodeManager(NodeManager &&) = delete;
  NodeManager &operator=(NodeManager &&) = delete;

  NodeManager(F2fs *fs);

  zx_status_t NextFreeNid(nid_t *nid);
  void NodeInfoFromRawNat(NodeInfoDeprecated &ni, RawNatEntry &raw_ne);
  zx_status_t BuildNodeManager();
  void DestroyNodeManager();
  zx::result<LockedPage> ReadNodePage(LockedPage page, nid_t nid, int type);
  zx_status_t GetNodePage(nid_t nid, LockedPage *out);

  zx::result<bool> IsSameDnode(VnodeF2fs &vnode, pgoff_t index, uint32_t node_offset);
  // If indices use the same node page, read the node page once and reuse it. This
  // can reduce repeated node page access overhead.
  // If |read_only| is true, it does not assign a new block address for kNullAddr.
  zx::result<std::vector<block_t>> GetDataBlockAddresses(VnodeF2fs &vnode, pgoff_t index,
                                                         size_t count, bool read_only = false);

  // If an unassigned node page is encountered while following the node path, a new node page is
  // assigned. Caller should acquire LockType:kFileOp.
  zx_status_t GetLockedDnodePage(VnodeF2fs &vnode, pgoff_t index, LockedPage *out);

  // Read-only mode of GetLockedDnodePage().
  zx_status_t FindLockedDnodePage(VnodeF2fs &vnode, pgoff_t index, LockedPage *out);

  zx::result<uint32_t> GetOfsInDnode(VnodeF2fs &vnode, pgoff_t index);

  zx_status_t RestoreNodeSummary(uint32_t segno, SummaryBlock &sum);

  static bool IsColdFile(VnodeF2fs &vnode);

  void GetNodeInfo(nid_t nid, NodeInfoDeprecated &out);

  // It flushes all dirty node Pages that meet |operation|.if_page.
  // It also removes dirty vnodes from the dirty list when there is no dirty Page for their vnodes
  // and data. To ensure there is no access to the vnodes, it is called with LockType::kFileOp held
  // during ckpt. This way guarantees that RecycleNode() for valid vnodes executes only at ckpt
  // time.
  pgoff_t SyncNodePages(WritebackOperation &operation);
  pgoff_t FsyncNodePages(VnodeF2fs &vnode);

  bool AllocNid(nid_t &out);
  void AllocNidFailed(nid_t nid);
  void AllocNidDone(nid_t nid);
  zx_status_t TruncateInodeBlocks(VnodeF2fs &vnode, pgoff_t from);

  // Caller should acquire LockType:kFileOp.
  zx_status_t RemoveInodePage(VnodeF2fs *vnode);
  // Caller should acquire LockType:kFileOp.
  zx::result<LockedPage> NewInodePage(VnodeF2fs &new_vnode);

  bool IsCheckpointedNode(nid_t nid);

  void DecValidNodeCount(VnodeF2fs *vnode, uint32_t count, bool isInode);
  bool FlushNatsInJournal();
  void FlushNatEntries();

  int F2fsWriteNodePage(LockedPage &page, bool is_reclaim = false);
  int F2fsWriteNodePages(VnodeF2fs &vnode, bool is_reclaim = false);

  zx_status_t RecoverInodePage(NodePage &page);

  // Check whether the given nid is within node id range.
  void CheckNidRange(const nid_t &nid) { ZX_ASSERT(nid < max_nid_); }

  // members for fsck and unit tests
  NodeManager(SuperblockInfo *sb);
  void SetMaxNid(const nid_t value) { max_nid_ = value; }
  nid_t GetMaxNid() const { return max_nid_; }
  void SetNatAddress(const block_t value) { nat_blkaddr_ = value; }
  block_t GetNatAddress() { return nat_blkaddr_; }
  void SetFirstScanNid(const nid_t value) { init_scan_nid_ = value; }
  nid_t GetFirstScanNid() const { return init_scan_nid_; }
  void SetNextScanNid(const nid_t value) { next_scan_nid_ = value; }
  nid_t GetNextScanNid() const { return next_scan_nid_; }
  nid_t GetNatCount() const { return nat_entries_count_; }
  nid_t GetFreeNidCount() const { return free_nid_count_; }
  zx_status_t AllocNatBitmap(const int size) {
    nat_bitmap_size_ = size;
    nat_bitmap_ = std::make_unique<uint8_t[]>(nat_bitmap_size_);
    memset(nat_bitmap_.get(), 0, nat_bitmap_size_);
    return ZX_OK;
  }
  void SetNatBitmap(const uint8_t *bitmap) { memcpy(nat_bitmap_.get(), bitmap, nat_bitmap_size_); }
  uint8_t *GetNatBitmap() const { return nat_bitmap_.get(); }
  void GetNatBitmap(void *out);

  SuperblockInfo &GetSuperblockInfo();

 private:
  friend class MapTester;
  bool IncValidNodeCount(VnodeF2fs *vnode, uint32_t count, bool isInode);

  pgoff_t CurrentNatAddr(nid_t start);
  bool IsUpdatedNatPage(nid_t start);
  pgoff_t NextNatAddr(pgoff_t block_addr);
  void SetToNextNat(nid_t start_nid);

  void GetCurrentNatPage(nid_t nid, LockedPage *out);
  void GetNextNatPage(nid_t nid, LockedPage *out);
  void RaNatPages(nid_t nid);

  void SetNatCacheDirty(NatEntry &ne) __TA_REQUIRES(nat_tree_lock_);
  void ClearNatCacheDirty(NatEntry &ne) __TA_REQUIRES(nat_tree_lock_);
  NatEntry *LookupNatCache(nid_t n) __TA_REQUIRES_SHARED(nat_tree_lock_);
  uint32_t GangLookupNatCache(uint32_t nr, NatEntry **out) __TA_REQUIRES_SHARED(nat_tree_lock_);
  void DelFromNatCache(NatEntry &entry) __TA_REQUIRES_SHARED(nat_tree_lock_);
  NatEntry *GrabNatEntry(nid_t nid) __TA_REQUIRES_SHARED(nat_tree_lock_);
  void CacheNatEntry(nid_t nid, RawNatEntry &raw_entry);
  void SetNodeAddr(NodeInfoDeprecated &ni, block_t new_blkaddr);
  int TryToFreeNats(int nr_shrink);

  zx::result<int32_t> GetNodePath(VnodeF2fs &vnode, pgoff_t block,
                                  int32_t (&offset)[kMaxNodeBlockLevel],
                                  uint32_t (&noffset)[kMaxNodeBlockLevel]);

  // Caller should ensure node_page is locked.
  void TruncateNode(VnodeF2fs &vnode, nid_t nid, NodePage &node_page);
  zx::result<uint32_t> TruncateDnode(VnodeF2fs &vnode, nid_t nid);
  zx::result<uint32_t> TruncateNodes(VnodeF2fs &vnode, nid_t start_nid, uint32_t nofs, int32_t ofs,
                                     int32_t depth);
  zx_status_t TruncatePartialNodes(VnodeF2fs &vnode, const Inode &ri,
                                   const int32_t (&offset)[kMaxNodeBlockLevel], int32_t depth);
  zx_status_t NewNodePage(VnodeF2fs &vnode, nid_t nid, uint32_t ofs, LockedPage *out);

  FreeNid *LookupFreeNidList(nid_t n);
  void DelFromFreeNidList(FreeNid *i);
  int AddFreeNid(nid_t nid);
  void RemoveFreeNid(nid_t nid);
  int ScanNatPage(Page &nat_page, nid_t start_nid);
  void BuildFreeNids();
  zx_status_t InitNodeManager();

  F2fs *fs_ = nullptr;
  SuperblockInfo *superblock_info_ = nullptr;
  block_t nat_blkaddr_ = 0;   // starting block address of NAT
  nid_t max_nid_ = 0;         // the maximum number of node ids
  nid_t init_scan_nid_ = 0;   // the first nid to be scanned
  nid_t next_scan_nid_ = 0;   // the next nid to be scanned
  nid_t free_nid_count_ = 0;  // the number of free node id

  fs::SharedMutex nat_tree_lock_;   // protect nat_tree_lock
  uint32_t nat_entries_count_ = 0;  // the number of nat cache entries

  using NatTreeTraits = fbl::DefaultKeyedObjectTraits<nid_t, NatEntry>;
  using NatTree = fbl::WAVLTree<nid_t, std::unique_ptr<NatEntry>, NatTreeTraits>;
  using NatList = fbl::DoublyLinkedList<NatEntry *>;

  NatTree nat_cache_ __TA_GUARDED(nat_tree_lock_);       // cached nat entries
  NatList clean_nat_list_ __TA_GUARDED(nat_tree_lock_);  // a list for cached clean nats
  NatList dirty_nat_list_ __TA_GUARDED(nat_tree_lock_);  // a list for cached dirty nats

  std::mutex free_nid_list_lock_;                                // protect free nid list
  list_node_t free_nid_list_ __TA_GUARDED(free_nid_list_lock_);  // a list for free nids
  std::mutex build_lock_;                                        // lock for building free nids

  std::unique_ptr<uint8_t[]> nat_bitmap_ = nullptr;       // NAT bitmap pointer
  std::unique_ptr<uint8_t[]> nat_prev_bitmap_ = nullptr;  // NAT previous checkpoint bitmap pointer
  int nat_bitmap_size_ = 0;                               // NAT bitmap size
};

}  // namespace f2fs

#endif  // SRC_STORAGE_F2FS_NODE_H_
