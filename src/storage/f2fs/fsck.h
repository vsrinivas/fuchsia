// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_F2FS_FSCK_H_
#define SRC_STORAGE_F2FS_FSCK_H_

#include <map>

namespace f2fs {

struct OrphanInfo {
  uint32_t nr_inodes = 0;
  uint32_t *ino_list = nullptr;
};

struct FsckInfo {
  OrphanInfo orphani;
  struct FsckResult {
    uint64_t valid_block_count = 0;
    uint32_t valid_nat_entry_count = 0;
    uint32_t valid_node_count = 0;
    uint32_t valid_inode_count = 0;
    uint32_t multi_hard_link_files = 0;
    uint64_t sit_valid_blocks = 0;
    uint32_t sit_free_segments = 0;
  } result;

  std::map<nid_t, uint32_t> hard_link_map;
  std::unique_ptr<uint8_t[]> main_area_bitmap;
  std::unique_ptr<uint8_t[]> nat_area_bitmap;
  std::unique_ptr<uint8_t[]> sit_area_bitmap;

  uint64_t main_area_bitmap_size = 0;
  uint32_t nat_area_bitmap_size = 0;
  uint32_t sit_area_bitmap_size = 0;
  uint64_t nr_main_blocks = 0;
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
  FsckWorker(const FsckWorker &) = delete;
  FsckWorker &operator=(const FsckWorker &) = delete;
  FsckWorker(FsckWorker &&) = delete;
  FsckWorker &operator=(FsckWorker &&) = delete;
  FsckWorker(std::unique_ptr<Bcache> bc) : tree_mark_(kDefaultDirTreeLen) { bc_ = std::move(bc); }

  zx_status_t CheckNodeBlock(Inode *inode, nid_t nid, FileType ftype, NodeType ntype,
                             uint32_t &block_count);
  zx_status_t CheckInodeBlock(nid_t nid, FileType ftype, Node &node_block, uint32_t &block_count,
                              NodeInfo &ni);
  zx_status_t CheckDataBlock(Inode *inode, uint32_t block_address, uint32_t &child_count,
                             uint32_t &child_files, int last_block, FileType ftype,
                             uint32_t parent_nid, uint16_t index_in_node, uint8_t ver);
  void CheckDnodeBlock(Inode *inode, nid_t nid, FileType ftype, Node &node_block,
                       uint32_t &block_count, NodeInfo &ni);
  void CheckIndirectNodeBlock(Inode *inode, nid_t nid, FileType ftype, Node &node_block,
                              uint32_t &block_count);
  void CheckDoubleIndirectNodeBlock(Inode *inode, nid_t nid, FileType ftype, Node &node_block,
                                    uint32_t &block_count);
  template <size_t bitmap_size, size_t entry_size>
  void CheckDentries(uint32_t &child_count, uint32_t &child_files, int last_block,
                     const uint8_t (&dentry_bitmap)[bitmap_size],
                     const DirEntry (&dentries)[entry_size], const uint8_t (*filename)[kNameLen],
                     int max_entries);
  void CheckDentryBlock(uint32_t block_address, uint32_t &child_count, uint32_t &child_files,
                        int last_block);

  void PrintRawSuperblockInfo();
  void PrintCheckpointInfo();
  void PrintNodeInfo(Node &node_block);
  void PrintInodeInfo(Inode &inode);
  template <size_t size>
  void PrintDentry(uint32_t depth, std::string_view name, const uint8_t (&dentry_bitmap)[size],
                   const DirEntry &dentries, int index, int last_block, int max_entries);

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
  void CheckOrphanNode();
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
  void DoUmount();
  zx_status_t Run();
  zx_status_t ReadBlock(FsBlock &fs_block, block_t bno);

  void InitSuperblockInfo();
  zx::status<std::pair<std::unique_ptr<FsBlock>, uint64_t>> ValidateCheckpoint(block_t cp_addr);
  zx_status_t SanityCheckRawSuper(const Superblock *raw_super);
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

  // Given a node segment index |segno|,
  // this function reads each block's footer in the segment and
  // restores nid part of entries in |summary_block|.
  zx_status_t RestoreNodeSummary(uint32_t segno, SummaryBlock &summary_block);
  std::pair<std::unique_ptr<FsBlock>, SegType> GetSumBlockInfo(uint32_t segno);
  std::pair<SegType, Summary> GetSummaryEntry(uint32_t block_address);
  void ResetCurseg(CursegType type, int modified);
  zx_status_t RestoreCursegSummaries();
  std::unique_ptr<FsBlock> GetCurrentSitPage(uint32_t segno);
  void SegmentInfoFromRawSit(SegmentEntry &segment_entry, const SitEntry &raw_sit);
  void CheckBlockCount(uint32_t segno, const SitEntry &raw_sit);
  zx::status<RawNatEntry> LookupNatInJournal(nid_t nid);
  zx::status<RawNatEntry> GetNatEntry(nid_t nid);
  inline void CheckSegmentRange(uint32_t segno);
  SegmentEntry &GetSegmentEntry(uint32_t segno);
  uint32_t GetSegmentNumber(uint32_t block_address);
  zx::status<NodeInfo> GetNodeInfo(nid_t nid);
  void AddIntoHardLinkMap(nid_t nid, uint32_t link_count);
  zx_status_t FindAndDecreaseHardLinkMap(nid_t nid);

  inline bool IsValidSsaNodeBlock(nid_t nid, uint32_t block_address);
  inline bool IsValidSsaDataBlock(uint32_t block_address, uint32_t parent_nid,
                                  uint16_t index_in_node, uint8_t version);
  bool IsValidNid(nid_t nid) {
    ZX_ASSERT(nid <= (kNatEntryPerBlock * superblock_info_.GetRawSuperblock().segment_count_nat
                      << (superblock_info_.GetLogBlocksPerSeg() - 1)));
    return true;
  }
  bool IsValidBlockAddress(uint32_t addr) {
    if (addr >= superblock_info_.GetRawSuperblock().block_count ||
        addr < segment_manager_->GetMainAreaStartBlock()) {
      ZX_ASSERT_MSG(addr < superblock_info_.GetRawSuperblock().block_count,
                    "block[0x%x] should be less than [0x%lx]\n", addr,
                    superblock_info_.GetRawSuperblock().block_count);
      ZX_ASSERT_MSG(addr >= segment_manager_->GetMainAreaStartBlock(),
                    "block[0x%x] should be larger than [0x%x]\n", addr,
                    segment_manager_->GetMainAreaStartBlock());
    }
    return true;
  }

  block_t StartSummaryBlock() {
    return superblock_info_.StartCpAddr() +
           LeToCpu(superblock_info_.GetCheckpoint().cp_pack_start_sum);
  }
  block_t SummaryBlockAddress(int base, int type) {
    return superblock_info_.StartCpAddr() +
           LeToCpu(superblock_info_.GetCheckpoint().cp_pack_total_block_count) - (base + 1) + type;
  }
  void NodeInfoFromRawNat(NodeInfo &ni, RawNatEntry &raw_nat) {
    ni.ino = LeToCpu(raw_nat.ino);
    ni.blk_addr = LeToCpu(raw_nat.block_addr);
    ni.version = raw_nat.version;
  }

#if 0  // porting needed
  int FsckChkXattrBlk(uint32_t ino, uint32_t x_nid, uint32_t *block_count);
  void sit_dump(SuperblockInfo *sbi, int start_sit, int end_sit);
  void ssa_dump(SuperblockInfo *sbi, int start_ssa, int end_ssa);
  int dump_node(SuperblockInfo *sbi, nid_t nid);
  int dump_inode_from_blkaddr(SuperblockInfo *sbi, uint32_t blk_addr);
#endif

  std::unique_ptr<Bcache> Destroy() { return std::move(bc_); }

 private:
  FsckInfo fsck_;
  SuperblockInfo superblock_info_;
  std::unique_ptr<NodeManager> node_manager_;
  std::unique_ptr<SegmentManager> segment_manager_;
  std::unique_ptr<Bcache> bc_;
  std::vector<char> tree_mark_;
};

zx_status_t Fsck(std::unique_ptr<Bcache> bc, std::unique_ptr<Bcache> *out = nullptr);

}  // namespace f2fs

#endif  // SRC_STORAGE_F2FS_FSCK_H_
