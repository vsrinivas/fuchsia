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

class NodeMgr {
 public:
  // Not copyable or moveable
  NodeMgr(const NodeMgr &) = delete;
  NodeMgr &operator=(const NodeMgr &) = delete;
  NodeMgr(NodeMgr &&) = delete;
  NodeMgr &operator=(NodeMgr &&) = delete;

  // TODO: Implement constructor
  NodeMgr(F2fs *fs);

  // TODO: Implement destructor
  ~NodeMgr() = default;

  // Public functions
  void SetFsyncMark(Page *page, int mark);
  void SetDentryMark(Page *page, int mark);

  zx_status_t NextFreeNid(nid_t *nid);
  void NodeInfoFromRawNat(NodeInfo &ni, RawNatEntry &raw_ne);
  static zx_status_t RestoreNodeSummary(F2fs *fs, uint32_t segno, SummaryBlock *sum);
  zx_status_t BuildNodeManager();
  void DestroyNodeManager();
  zx_status_t ReadNodePage(Page *page, nid_t nid, int type);
  zx_status_t GetNodePage(nid_t nid, Page **out);

  // Caller should acquire LockType:kFileOp when |ro| = 0.
  zx_status_t GetDnodeOfData(DnodeOfData *dn, pgoff_t index, int ro);

  void FillNodeFooter(Page *page, nid_t nid, nid_t ino, uint32_t ofs, bool reset);
  void CopyNodeFooter(Page *dst, Page *src);

  uint32_t OfsOfNode(Page *node_page);

  static int IsColdNode(Page *page);
  static bool IsColdFile(VnodeF2fs *vnode);
  static int IsColdData(Page *page);

  uint8_t IsDentDnode(Page *page);
  uint8_t IsFsyncDnode(Page *page);

  uint64_t CpverOfNode(Page *node_page);

  void FillNodeFooterBlkaddr(Page *page, block_t blkaddr);
  static block_t NextBlkaddrOfNode(Page *node_page);

  nid_t InoOfNode(Page *node_page);
  nid_t NidOfNode(Page *node_page);

  bool IS_DNODE(Page *node_page);
  void GetNodeInfo(nid_t nid, NodeInfo *ni);
  int SyncNodePages(nid_t ino, WritebackControl *wbc);
  void SyncInodePage(DnodeOfData *dn);

  bool AllocNid(nid_t *nid);
  void AllocNidFailed(nid_t nid);
  void AllocNidDone(nid_t nid);
  zx_status_t TruncateInodeBlocks(VnodeF2fs *vnode, pgoff_t from);

  // Caller should acquire LockType:kFileOp.
  zx_status_t RemoveInodePage(VnodeF2fs *vnode);
  // Caller should acquire LockType:kFileOp.
  zx_status_t NewInodePage(Dir *parent, VnodeF2fs *child);

  bool IsCheckpointedNode(nid_t nid);

  void ClearColdData(Page *page);

  void DecValidNodeCount(SbInfo *sbis, VnodeF2fs *vnode, uint32_t count);
  void GetNatBitmap(void *addr);
  bool FlushNatsInJournal();
  void FlushNatEntries();

  int F2fsWriteNodePage(Page *page, WritebackControl *wbc);
  int F2fsWriteNodePages(struct address_space *mapping, WritebackControl *wbc);
  zx_status_t RecoverInodePage(Page *page);
  void RecoverNodePage(Page *page, Summary *sum, NodeInfo *ni, block_t new_blkaddr);

  void SetColdNode(VnodeF2fs *vnode, Page *page);

  // It returns the starting file offset that |node_page| indicates.
  // The file offset can be calcuated by using the node offset that |node_page| has.
  // See NodeMgt::IS_DNODE().
  block_t StartBidxOfNode(Page *node_page);

 private:
  F2fs *fs_;

  // Inline functions
  bool IncValidNodeCount(SbInfo *sbi, VnodeF2fs *vnode, uint32_t count);

  pgoff_t CurrentNatAddr(nid_t start);
  bool IsUpdatedNatPage(nid_t start);
  pgoff_t NextNatAddr(pgoff_t block_addr);
  void SetToNextNat(NmInfo *nm_i, nid_t start_nid);

  void SetNid(Page *p, int off, nid_t nid, bool i);
  nid_t GetNid(Page *p, int off, bool i);

  // Functions
  void ClearNodePageDirty(Page *page);
  Page *GetCurrentNatPage(nid_t nid);
  Page *GetNextNatPage(nid_t nid);
  void RaNatPages(nid_t nid);

  NatEntry *LookupNatCache(NmInfo &nm_i, nid_t n) __TA_REQUIRES_SHARED(nm_i.nat_tree_lock);
  uint32_t GangLookupNatCache(NmInfo &nm_i, uint32_t nr, NatEntry **ep)
      __TA_REQUIRES_SHARED(nm_i.nat_tree_lock);
  void DelFromNatCache(NmInfo &nm_i, NatEntry &entry) __TA_REQUIRES_SHARED(nm_i.nat_tree_lock);
  NatEntry *GrabNatEntry(NmInfo &nm_i, nid_t nid) __TA_REQUIRES_SHARED(nm_i.nat_tree_lock);
  void CacheNatEntry(NmInfo &nm_i, nid_t nid, RawNatEntry &ne);
  void SetNodeAddr(NodeInfo *ni, block_t new_blkaddr);
  int TryToFreeNats(int nr_shrink);

  zx::status<int> GetNodePath(long block, int offset[4], uint32_t noffset[4]);
  void TruncateNode(DnodeOfData *dn);
  zx_status_t TruncateDnode(DnodeOfData *dn);
  zx_status_t TruncateNodes(DnodeOfData *dn, uint32_t nofs, int ofs, int depth);
  zx_status_t TruncatePartialNodes(DnodeOfData *dn, Inode *ri, int *offset, int depth);

  zx_status_t NewNodePage(DnodeOfData *dn, uint32_t ofs, Page **out);
  Page *GetNodePageRa(Page *parent, int start);

#if 0  // porting needed
  void SetColdData(Page &page);
  void RaNodePage(nid_t nid);

  int F2fsWriteNodePages(address_space *mapping,
            WritebackControl *wbc);
  int F2fsSetNodePageDirty(Page *page);
  void F2fsInvalidateNodePage(Page *page, uint64_t offset);
  int F2fsReleaseNodePage(Page *page, gfp_t wait);
#endif

  FreeNid *LookupFreeNidList(nid_t n, list_node_t *head);
  void DelFromFreeNidList(FreeNid *i);
  int AddFreeNid(NmInfo *nm_i, nid_t nid);
  void RemoveFreeNid(NmInfo *nm_i, nid_t nid);
  int ScanNatPage(NmInfo *nm_i, Page *nat_page, nid_t start_nid);
  void BuildFreeNids();

  zx_status_t InitNodeManager();
  zx_status_t CreateNodeManagerCaches();
  void DestroyNodeManagerCaches();
};

}  // namespace f2fs

#endif  // SRC_STORAGE_F2FS_NODE_H_
