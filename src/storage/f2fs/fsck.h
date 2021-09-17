// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_F2FS_FSCK_H_
#define SRC_STORAGE_F2FS_FSCK_H_

namespace f2fs {

struct OrphanInfo {
  uint32_t nr_inodes = 0;
  uint32_t *ino_list = nullptr;
};

struct HardLinkNode {
  uint32_t nid = 0;
  uint32_t links = 0;
  HardLinkNode *next = nullptr;
};

struct FsckInfo {
  OrphanInfo orphani;
  struct chk_result {
    uint64_t valid_blk_cnt = 0;
    uint32_t valid_nat_entry_cnt = 0;
    uint32_t valid_node_cnt = 0;
    uint32_t valid_inode_cnt = 0;
    uint32_t multi_hard_link_files = 0;
    uint64_t sit_valid_blocks = 0;
    uint32_t sit_free_segs = 0;
  } chk;

  HardLinkNode *hard_link_list_head = nullptr;

  uint8_t *main_seg_usage = nullptr;
  uint8_t *main_area_bitmap = nullptr;
  uint8_t *nat_area_bitmap = nullptr;
  uint8_t *sit_area_bitmap = nullptr;

  uint64_t main_area_bitmap_sz = 0;
  uint32_t nat_area_bitmap_sz = 0;
  uint32_t sit_area_bitmap_sz = 0;

  uint64_t nr_main_blks = 0;
  uint32_t nr_nat_entries = 0;

  uint32_t dentry_depth = 0;
};

enum class NodeType {
  kTypeInode = 37,
  kTypeDirectNode = 43,
  kTypeIndirectNode = 53,
  kTypeDoubleIndirectNode = 67
};

enum class SegType {
  kSegTypeData = 0,
  kSegTypeCurData,
  kSegTypeNode,
  kSegTypeCurNode,
  kSegTypeMax,
};

#if 0  // porting needed
struct DumpOption {
  nid_t nid;
  int start_sit;
  int end_sit;
  int start_ssa;
  int end_ssa;
  uint32_t blk_addr;
};
#endif

constexpr uint32_t kDefaultDirTreeLen = 256;

class FsckWorker {
 public:
  // Not copyable or movable
  FsckWorker() = delete;
  FsckWorker(const FsckWorker &) = delete;
  FsckWorker &operator=(const FsckWorker &) = delete;
  FsckWorker(FsckWorker &&) = delete;
  FsckWorker &operator=(FsckWorker &&) = delete;
  FsckWorker(Bcache *bc) : bc_(bc), tree_mark_(kDefaultDirTreeLen) {}

  zx_status_t ChkNodeBlk(Inode *inode, uint32_t nid, FileType ftype, NodeType ntype,
                         uint32_t *blk_cnt);
  zx_status_t ChkInodeBlk(uint32_t nid, FileType ftype, Node *node_blk, uint32_t *blk_cnt,
                          NodeInfo *ni);
  zx_status_t ChkDataBlk(Inode *inode, uint32_t block_address, uint32_t *child_cnt,
                         uint32_t *child_files, int last_blk, FileType ftype, uint32_t parent_nid,
                         uint16_t idx_in_node, uint8_t ver);
  void ChkDnodeBlk(Inode *inode, uint32_t nid, FileType ftype, Node *node_blk, uint32_t *blk_cnt,
                   NodeInfo *ni);
  void ChkIdnodeBlk(Inode *inode, uint32_t nid, FileType ftype, Node *node_blk, uint32_t *blk_cnt);
  void ChkDidnodeBlk(Inode *inode, uint32_t nid, FileType ftype, Node *node_blk, uint32_t *blk_cnt);
  template <size_t bitmap_size, size_t entry_size>
  void ChkDentries(uint32_t *const child_cnt, uint32_t *const child_files, const int last_blk,
                   const uint8_t (&dentry_bitmap)[bitmap_size],
                   const DirEntry (&dentries)[entry_size], const uint8_t (*filename)[kNameLen],
                   const int max_entries);
  void ChkDentryBlk(uint32_t block_address, uint32_t *child_cnt, uint32_t *child_files,
                    int last_blk);

  void PrintRawSbInfo();
  void PrintCkptInfo();
  void PrintNodeInfo(Node *node_block);
  void PrintInodeInfo(Inode *inode);
  template <size_t size>
  void PrintDentry(const uint32_t depth, const std::string_view name,
                   const uint8_t (&dentry_bitmap)[size], const DirEntry &dentries, const int idx,
                   const int last_blk, const int max_entries);

  // Fsck checks f2fs consistency as below.
  // 1. It loads a valid superblock, and it obtains valid node/inode/block count information.
  zx_status_t DoMount();
  // 2. It builds three bitmap:
  //  a) main_area_bitmap indicates valid blocks that DoFsck() will identify.
  //  b) nat_area_bitmap indicates used NIDs retrieved from NAT.
  //     Once DoFsck() identifies a valid NID, it clears the bit.
  //  c) sit_area_bitmap indicates valid blocks retrieved from SIT.
  // DoFsck() references it for checking the block validity.
  zx_status_t Init();
  // 3. It checks orphan nodes, and it updates nat_area_bitmap.
  void ChkOrphanNode();
  // 4. It traverses blocks from the root inode to leaf inodes to check the validity of
  //   the data/node blocks based on SSA and SIT and to update nat_area_bitmap and main_area_bitmap.
  //   In case of dir block, it checks the validity of child dentries and regarding inodes.
  //   It tracks various count information as well.
  zx_status_t DoFsck();
  // 5. It determines the consistency:
  //   a) main_area_bitmap must be the same as sit_area_bitmap
  //   b) all bits in nat_area_bitmap must be clear. That is, no dangling NIDs.
  //   c) The count information that DoFsck() retrieves must be the same as that in 1.
  //   d) no unreachable links
  zx_status_t Verify();
  void Free();
  void DoUmount();
  zx_status_t Run();
  zx_status_t ReadBlock(void *data, uint64_t bno);

  void InitSbInfo();
  void *ValidateCheckpoint(block_t cp_addr, uint64_t *version);
  zx_status_t SanityCheckRawSuper(const SuperBlock *raw_super);
  zx_status_t ValidateSuperblock(block_t block);
  zx_status_t GetValidCheckpoint();
  zx_status_t SanityCheckCkpt();
  zx_status_t InitNodeManager();
  zx_status_t BuildNodeManager();
  zx_status_t BuildSitInfo();
  zx_status_t BuildCurseg();
  zx_status_t BuildSegmentManager();
  void BuildNatAreaBitmap();
  void BuildSitAreaBitmap();
  void BuildSitEntries();

  zx_status_t ReadCompactedSummaries();
  zx_status_t ReadNormalSummaries(CursegType type);
  zx_status_t RestoreNodeSummary(unsigned int segno, SummaryBlock *sum_blk);
  SegType GetSumBlockInfo(uint32_t segno, SummaryBlock *sum_blk);
  SegType GetSumEntry(uint32_t block_address, Summary *sum_entry);
  void ResetCurseg(CursegType type, int modified);
  zx_status_t RestoreCursegSummaries();
  SitBlock *GetCurrentSitPage(unsigned int segno);
  void SegInfoFromRawSit(SegEntry *se, SitEntry *raw_sit);
  void CheckBlockCount(uint32_t segno, SitEntry *raw_sit);
  zx::status<int> LookupNatInJournal(uint32_t nid, RawNatEntry *raw_nat);
  zx_status_t GetNatEntry(nid_t nid, RawNatEntry *raw_nat);
  inline void ChkSegRange(unsigned int segno);
  SegEntry *GetSegEntry(unsigned int segno);
  uint32_t GetSegNo(uint32_t block_address);
  zx_status_t GetNodeInfo(nid_t nid, NodeInfo *ni);
  void AddIntoHardLinkList(uint32_t nid, uint32_t link_cnt);
  zx_status_t FindAndDecHardLinkList(uint32_t nid);

  inline bool IsValidSsaNodeBlk(uint32_t nid, uint32_t block_address);
  inline bool IsValidSsaDataBlk(uint32_t block_address, uint32_t parent_nid, uint16_t idx_in_node,
                                uint8_t version);
  inline bool IsValidNid(uint32_t nid) {
    ZX_ASSERT(nid <= (kNatEntryPerBlock * RawSuper(&sbi_)->segment_count_nat
                      << (sbi_.log_blocks_per_seg - 1)));
    return true;
  }
  inline bool IsValidBlkAddr(uint32_t addr) {
    if (addr >= RawSuper(&sbi_)->block_count || addr < GetSmInfo(&sbi_)->main_blkaddr) {
      ZX_ASSERT_MSG(addr < RawSuper(&sbi_)->block_count, "block addr [0x%x]\n", addr);
      ZX_ASSERT_MSG(addr >= GetSmInfo(&sbi_)->main_blkaddr, "block addr [0x%x]\n", addr);
    }
    return true;
  }

  inline block_t StartSumBlock() {
    return StartCpAddr(&sbi_) + LeToCpu(GetCheckpoint(&sbi_)->cp_pack_start_sum);
  }
  inline block_t SumBlkAddr(int base, int type) {
    return StartCpAddr(&sbi_) + LeToCpu(GetCheckpoint(&sbi_)->cp_pack_total_block_count) -
           (base + 1) + type;
  }
  inline void NodeInfoFromRawNat(NodeInfo *ni, RawNatEntry *raw_nat) {
    ni->ino = LeToCpu(raw_nat->ino);
    ni->blk_addr = LeToCpu(raw_nat->block_addr);
    ni->version = raw_nat->version;
  }

#if 0  // porting needed
  int FsckChkXattrBlk(uint32_t ino, uint32_t x_nid, uint32_t *blk_cnt);
  void sit_dump(SbInfo *sbi, int start_sit, int end_sit);
  void ssa_dump(SbInfo *sbi, int start_ssa, int end_ssa);
  int dump_node(SbInfo *sbi, nid_t nid);
  int dump_inode_from_blkaddr(SbInfo *sbi, uint32_t blk_addr);
#endif

 private:
  FsckInfo fsck_;
  SbInfo sbi_;
  std::unique_ptr<NodeManager> node_manager_;
  Bcache *bc_;
  std::vector<char> tree_mark_;
};

zx_status_t Fsck(Bcache *bc);

}  // namespace f2fs

#endif  // SRC_STORAGE_F2FS_FSCK_H_
