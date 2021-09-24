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

// For node information
struct NodeInfo {
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

  const NodeInfo &GetNodeInfo() { return ni_; }
  void SetNodeInfo(const NodeInfo &value) { ni_ = value; }

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
  NodeInfo ni_;                // in-memory node information
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
  void NodeInfoFromRawNat(NodeInfo &ni, RawNatEntry &raw_ne);
  zx_status_t BuildNodeManager();
  void DestroyNodeManager();
  zx_status_t ReadNodePage(Page &page, nid_t nid, int type);
  zx_status_t GetNodePage(nid_t nid, Page **out);

  // Caller should acquire LockType:kFileOp when |ro| = 0.
  zx_status_t GetDnodeOfData(DnodeOfData &dn, pgoff_t index, int ro);
  void FillNodeFooterBlkaddr(Page *page, block_t blkaddr);

  static zx_status_t RestoreNodeSummary(F2fs &fs, uint32_t segno, SummaryBlock &sum);

  static void FillNodeFooter(Page &page, nid_t nid, nid_t ino, uint32_t ofs, bool reset);
  static void CopyNodeFooter(Page &dst, Page &src);

  static uint32_t OfsOfNode(Page &node_page);
  static int IsColdNode(Page &page);
  static bool IsColdFile(VnodeF2fs &vnode);
  static uint8_t IsDentDnode(Page &page);
  static uint8_t IsFsyncDnode(Page &page);
  static uint64_t CpverOfNode(Page &node_page);
  static block_t NextBlkaddrOfNode(Page &node_page);
  static nid_t InoOfNode(Page &node_page);
  static nid_t NidOfNode(Page &node_page);
  static void SetColdNode(VnodeF2fs &vnode, Page &page);
  static bool IS_DNODE(Page &node_page);
  static void SetFsyncMark(Page &page, int mark);
  static void SetDentryMark(Page &page, int mark);
  // It returns the starting file offset that |node_page| indicates.
  // The file offset can be calcuated by using the node offset that |node_page| has.
  // See NodeMgt::IS_DNODE().
  static block_t StartBidxOfNode(Page &node_page);
  static void SetNewDnode(DnodeOfData &dn, VnodeF2fs *vnode, Page *ipage, Page *npage, nid_t nid) {
    dn.vnode = vnode;
    dn.inode_page = ipage;
    dn.node_page = npage;
    dn.nid = nid;
    dn.inode_page_locked = 0;
  }

  void GetNodeInfo(nid_t nid, NodeInfo &out);
  int SyncNodePages(nid_t ino, WritebackControl *wbc);
  void SyncInodePage(DnodeOfData &dn);

  bool AllocNid(nid_t *out);
  void AllocNidFailed(nid_t nid);
  void AllocNidDone(nid_t nid);
  zx_status_t TruncateInodeBlocks(VnodeF2fs &vnode, pgoff_t from);

  // Caller should acquire LockType:kFileOp.
  zx_status_t RemoveInodePage(VnodeF2fs *vnode);
  // Caller should acquire LockType:kFileOp.
  zx_status_t NewInodePage(Dir *parent, VnodeF2fs *child);

  bool IsCheckpointedNode(nid_t nid);

  void DecValidNodeCount(VnodeF2fs *vnode, uint32_t count);
  bool FlushNatsInJournal();
  void FlushNatEntries();

  int F2fsWriteNodePage(Page &page, WritebackControl *wbc);
  int F2fsWriteNodePages(struct address_space *mapping, WritebackControl *wbc);

  zx_status_t RecoverInodePage(Page &page);
  void RecoverNodePage(Page &page, Summary &sum, NodeInfo &ni, block_t new_blkaddr);

  // Check whether the given nid is within node id range.
  void CheckNidRange(const nid_t &nid) { ZX_ASSERT(nid < max_nid_); }

  // members for fsck and unit tests
  NodeManager(SbInfo *sb);
  void SetMaxNid(const nid_t value) { max_nid_ = value; }
  nid_t GetMaxNid() const { return max_nid_; }
  void SetNatAddress(const block_t value) { nat_blkaddr_ = value; }
  nid_t GetNatAddress() { return nat_blkaddr_; }
  void SetFirstScanNid(const nid_t value) { init_scan_nid_ = value; }
  nid_t GetFirstScanNid() const { return init_scan_nid_; }
  void SetNextScanNid(const nid_t value) { next_scan_nid_ = value; }
  nid_t GetNextScanNid() const { return next_scan_nid_; }
  nid_t GetNatCount() const { return nat_entries_count_; }
  nid_t GetFreeNidCount() const { return free_nid_count_; }
  zx_status_t AllocNatBitmap(const int size) {
    nat_bitmap_size_ = size;
    if (nat_bitmap_ = std::make_unique<uint8_t[]>(nat_bitmap_size_); nat_bitmap_ == nullptr) {
      return ZX_ERR_NO_MEMORY;
    }
    memset(nat_bitmap_.get(), 0, nat_bitmap_size_);
    return ZX_OK;
  }
  void SetNatBitmap(const uint8_t *bitmap) { memcpy(nat_bitmap_.get(), bitmap, nat_bitmap_size_); }
  uint8_t *GetNatBitmap() const { return nat_bitmap_.get(); }
  void GetNatBitmap(void *addr);

 private:
  friend class MapTester;
  bool IncValidNodeCount(VnodeF2fs *vnode, uint32_t count);

  pgoff_t CurrentNatAddr(nid_t start);
  bool IsUpdatedNatPage(nid_t start);
  pgoff_t NextNatAddr(pgoff_t block_addr);
  void SetToNextNat(nid_t start_nid);

  void SetNid(Page &p, int off, nid_t nid, bool i);
  nid_t GetNid(Page &p, int off, bool i);

  void ClearNodePageDirty(Page *page);
  Page *GetCurrentNatPage(nid_t nid);
  Page *GetNextNatPage(nid_t nid);
  void RaNatPages(nid_t nid);

  void SetNatCacheDirty(NatEntry &ne) __TA_REQUIRES(nat_tree_lock_);
  void ClearNatCacheDirty(NatEntry &ne) __TA_REQUIRES(nat_tree_lock_);
  NatEntry *LookupNatCache(nid_t n) __TA_REQUIRES_SHARED(nat_tree_lock_);
  uint32_t GangLookupNatCache(uint32_t nr, NatEntry **ep) __TA_REQUIRES_SHARED(nat_tree_lock_);
  void DelFromNatCache(NatEntry &entry) __TA_REQUIRES_SHARED(nat_tree_lock_);
  NatEntry *GrabNatEntry(nid_t nid) __TA_REQUIRES_SHARED(nat_tree_lock_);
  void CacheNatEntry(nid_t nid, RawNatEntry &ne);
  void SetNodeAddr(NodeInfo &ni, block_t new_blkaddr);
  int TryToFreeNats(int nr_shrink);

  zx::status<int> GetNodePath(VnodeF2fs &vnode, long block, int (&offset)[4], uint32_t (&noffset)[4]);
  void TruncateNode(DnodeOfData &dn);
  zx_status_t TruncateDnode(DnodeOfData &dn);
  zx_status_t TruncateNodes(DnodeOfData &dn, uint32_t nofs, int ofs, int depth);
  zx_status_t TruncatePartialNodes(DnodeOfData &dn, Inode &ri, int (&offset)[4], int depth);
  zx_status_t NewNodePage(DnodeOfData &dn, uint32_t ofs, Page **out);

#if 0  // porting needed
  static int IsColdData(Page &page);
  static void ClearColdData(Page &page);
  Page *GetNodePageRa(Page *parent, int start);
  void SetColdData(Page &page);
  void RaNodePage(nid_t nid);

  int F2fsWriteNodePages(address_space *mapping,
            WritebackControl *wbc);
  int F2fsSetNodePageDirty(Page *page);
  void F2fsInvalidateNodePage(Page *page, uint64_t offset);
  int F2fsReleaseNodePage(Page *page, gfp_t wait);
#endif

  FreeNid *LookupFreeNidList(nid_t n);
  void DelFromFreeNidList(FreeNid *i);
  int AddFreeNid(nid_t nid);
  void RemoveFreeNid(nid_t nid);
  int ScanNatPage(Page &nat_page, nid_t start_nid);
  void BuildFreeNids();
  zx_status_t InitNodeManager();

  F2fs *fs_ = nullptr;
  SbInfo *sbi_ = nullptr;
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

  fs::SharedMutex free_nid_list_lock_;                           // protect free nid list
  list_node_t free_nid_list_ __TA_GUARDED(free_nid_list_lock_);  // a list for free nids
  fbl::Mutex build_lock_;                                        // lock for building free nids

  std::unique_ptr<uint8_t[]> nat_bitmap_ = nullptr;       // NAT bitmap pointer
  std::unique_ptr<uint8_t[]> nat_prev_bitmap_ = nullptr;  // NAT previous checkpoint bitmap pointer
  int nat_bitmap_size_ = 0;                               // NAT bitmap size
};

}  // namespace f2fs

#endif  // SRC_STORAGE_F2FS_NODE_H_
