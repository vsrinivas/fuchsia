// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef THIRD_PARTY_F2FS_NODE_H_
#define THIRD_PARTY_F2FS_NODE_H_

namespace f2fs {

/* start node id of a node block dedicated to the given node id */
inline uint32_t StartNid(uint32_t nid) { return (nid / kNatEntryPerBlock) * kNatEntryPerBlock; }

/* node block offset on the NAT area dedicated to the given start node id */
inline uint64_t NatBlockOffset(uint32_t start_nid) { return start_nid / kNatEntryPerBlock; }

/* # of pages to perform readahead before building free nids */
constexpr int kFreeNidPages = 4;

/* maximum # of free node ids to produce during build_free_nids */
constexpr int kMaxFreeNids = kNatEntryPerBlock * kFreeNidPages;

/* maximum readahead size for node during getting data blocks */
constexpr int kMaxRaNode = 128;

/* maximum cached nat entries to manage memory footprint */
constexpr uint32_t kNmWoutThreshold = 64 * kNatEntryPerBlock;

/* vector size for gang look-up from nat cache that consists of radix tree */
constexpr uint32_t kNatvecSize = 64;

/*
 * For node information
 */
struct NodeInfo {
  nid_t nid = 0;        /* node id */
  nid_t ino = 0;        /* inode number of the node's owner */
  block_t blk_addr = 0; /* block address of the node */
  uint8_t version = 0;  /* version of the node */
};

struct NatEntry {
  list_node_t list;          /* for clean or dirty nat list */
  bool checkpointed = false; /* whether it is checkpointed or not */
  NodeInfo ni;               /* in-memory node information */
};

inline uint32_t NatGetNid(NatEntry *nat) { return nat->ni.nid; }
inline void NatSetNid(NatEntry *nat, nid_t n) { nat->ni.nid = n; }
inline block_t NatGetBlkaddr(NatEntry *nat) { return nat->ni.blk_addr; }
inline void NatSetBlkaddr(NatEntry *nat, block_t b) { nat->ni.blk_addr = b; }
inline uint32_t NatGetIno(NatEntry *nat) { return nat->ni.ino; }
inline void NatSetIno(NatEntry *nat, uint32_t i) { nat->ni.ino = i; }
inline uint8_t NatGetVersion(NatEntry *nat) { return nat->ni.version; }
inline void NatSetVersion(NatEntry *nat, uint8_t v) { nat->ni.version = v; }

inline uint8_t IncNodeVersion(uint8_t version) { return ++version; }

/*
 * For free nid mangement
 */
enum class NidState {
  kNidNew = 0, /* newly added to free nid list */
  kNidAlloc,   /* it is allocated */
};

struct FreeNid {
  list_node_t list; /* for free node id list */
  nid_t nid = 0;    /* node id */
  int state = 0;    /* in use or not: kNidNew or kNidAlloc */
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
  void NodeInfoFromRawNat(NodeInfo *ni, RawNatEntry *raw_ne);
  static zx_status_t RestoreNodeSummary(F2fs *fs, uint32_t segno, SummaryBlock *sum);
  zx_status_t BuildNodeManager();
  void DestroyNodeManager();
  zx_status_t ReadNodePage(Page *page, nid_t nid, int type);
  zx_status_t GetNodePage(pgoff_t nid, Page **out);

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

  int IsCheckpointedNode(nid_t nid);

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

#if 0  // porting needed
  void SetColdData(Page *page);
#endif

  // Functions
  void ClearNodePageDirty(Page *page);
  Page *GetCurrentNatPage(nid_t nid);
  Page *GetNextNatPage(nid_t nid);
  void RaNatPages(nid_t nid);
  NatEntry *LookupNatCache(NmInfo *nm_i, nid_t n);
  uint32_t GangLookupNatCache(NmInfo *nm_i, nid_t start, uint32_t nr, NatEntry **ep);
  void DelFromNatCache(NmInfo *nm_i, NatEntry *e);

  NatEntry *GrabNatEntry(NmInfo *nm_i, nid_t nid);
  void CacheNatEntry(NmInfo *nm_i, nid_t nid, RawNatEntry *ne);
  void SetNodeAddr(NodeInfo *ni, block_t new_blkaddr);
  int TryToFreeNats(int nr_shrink);

  zx::status<int> GetNodePath(long block, int offset[4], uint32_t noffset[4]);
  void TruncateNode(DnodeOfData *dn);
  zx_status_t TruncateDnode(DnodeOfData *dn);
  zx_status_t TruncateNodes(DnodeOfData *dn, uint32_t nofs, int ofs, int depth);
  zx_status_t TruncatePartialNodes(DnodeOfData *dn, Inode *ri, int *offset, int depth);

  zx_status_t NewNodePage(DnodeOfData *dn, uint32_t ofs, Page **out);
#if 0  // porting needed
  void RaNodePage(nid_t nid);
#endif
  Page *GetNodePageRa(Page *parent, int start);

#if 0  // porting needed
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

#endif  // THIRD_PARTY_F2FS_NODE_H_
