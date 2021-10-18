// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <bitset>
#include <iostream>

#include <safemath/checked_math.h>

#include "src/storage/f2fs/f2fs.h"

namespace f2fs {

template <typename T>
static inline void DisplayMember(uint32_t typesize, T value, std::string name) {
  if (typesize == sizeof(char)) {
    std::cout << name << " [" << value << "]" << std::endl;
  } else {
    ZX_ASSERT(sizeof(T) <= typesize);
    std::cout << name << " [0x" << std::hex << value << " : " << std::dec << value << "]"
              << std::endl;
  }
}

static int32_t operator-(CursegType &a, CursegType &&b) {
  return (static_cast<int32_t>(a) - static_cast<int32_t>(b));
}

static bool operator<=(int32_t &a, CursegType &&b) { return (a <= static_cast<int32_t>(b)); }

CursegType operator+(CursegType a, uint32_t &&b) {
  return static_cast<CursegType>(static_cast<uint32_t>(a) + b);
}

static inline bool IsSumNodeSeg(SummaryFooter &footer) { return footer.entry_type == kSumTypeNode; }

static inline uint64_t BlkoffFromMain(SegmentManager &manager, uint64_t block_address) {
  ZX_ASSERT(block_address >= manager.GetMainAreaStartBlock());
  return block_address - manager.GetMainAreaStartBlock();
}

static inline uint32_t OffsetInSegment(SuperblockInfo &sbi, SegmentManager &manager,
                                       uint64_t block_address) {
  return (uint32_t)(BlkoffFromMain(manager, block_address) % (1 << sbi.GetLogBlocksPerSeg()));
}

static inline uint16_t AddrsPerInode(Inode *i) {
#if 0  // porting needed
	      if (i->i_inline & kInlineXattr)
					            return kAddrPerInode - kInlineXattrAddrs;
#endif
  return kAddrsPerInode;
}

zx_status_t FsckWorker::ReadBlock(FsBlock &fs_block, block_t bno) {
#ifdef __Fuchsia__
  return bc_->Readblk(bno, fs_block.GetData().data());
#else   // __Fuchsia__
  return bc_->Readblk(bno, fs_block.GetData());
#endif  // __Fuchsia__
}

void FsckWorker::AddIntoHardLinkMap(nid_t nid, uint32_t link_count) {
  auto ret = fsck_.hard_link_map.insert({nid, link_count});
  ZX_ASSERT(ret.second);
  FX_LOGS(INFO) << "ino[0x" << std::hex << nid << "] has hard links [0x" << link_count << "]";
}

zx_status_t FsckWorker::FindAndDecreaseHardLinkMap(nid_t nid) {
  if (auto ret = fsck_.hard_link_map.find(nid); ret != fsck_.hard_link_map.end()) {
    if (--ret->second; ret->second == 1) {
      fsck_.hard_link_map.erase(ret);
    }
    return ZX_OK;
  }
  return ZX_ERR_NOT_FOUND;
}

bool FsckWorker::IsValidSsaNodeBlock(nid_t nid, uint32_t block_address) {
  auto [ret, summary_entry] = GetSummaryEntry(block_address);
  ZX_ASSERT(static_cast<int>(ret) >= 0);

  if (ret == SegType::kSegTypeData || ret == SegType::kSegTypeCurData) {
    FX_LOGS(ERROR) << "Summary footer is not a node segment summary";
    ZX_ASSERT(0);
  } else if (ret == SegType::kSegTypeNode) {
    if (LeToCpu(summary_entry.nid) != nid) {
      FX_LOGS(ERROR) << "nid                       [0x" << std::hex << nid << "]";
      FX_LOGS(ERROR) << "target block_address           [0x" << std::hex << block_address << "]";
      FX_LOGS(ERROR) << "summary block_address          [0x" << std::hex
                     << segment_manager_->GetSumBlock(
                            segment_manager_->GetSegmentNumber(block_address))
                     << "]";
      FX_LOGS(ERROR) << "seg no / offset           [0x" << std::hex
                     << segment_manager_->GetSegmentNumber(block_address) << "/0x" << std::hex
                     << OffsetInSegment(superblock_info_, *segment_manager_, block_address) << "]";
      FX_LOGS(ERROR) << "summary_entry.nid         [0x" << std::hex << LeToCpu(summary_entry.nid)
                     << "]";
      FX_LOGS(ERROR) << "--> node block's nid      [0x" << std::hex << nid << "]";
      FX_LOGS(ERROR) << "Invalid node seg summary\n";
      ZX_ASSERT(0);
    }
  } else if (ret == SegType::kSegTypeCurNode) {
    // current node segment has no ssa
  } else {
    FX_LOGS(ERROR) << "Invalid return value of 'GetSummaryEntry'";
    ZX_ASSERT(0);
  }
  return true;
}

bool FsckWorker::IsValidSsaDataBlock(uint32_t block_address, uint32_t parent_nid,
                                     uint16_t index_in_node, uint8_t version) {
  auto [ret, summary_entry] = GetSummaryEntry(block_address);
  ZX_ASSERT(ret == SegType::kSegTypeData || ret == SegType::kSegTypeCurData);

  if (LeToCpu(summary_entry.nid) != parent_nid || summary_entry.version != version ||
      LeToCpu(summary_entry.ofs_in_node) != index_in_node) {
    FX_LOGS(ERROR) << "summary_entry.nid         [0x" << std::hex << LeToCpu(summary_entry.nid)
                   << "]";
    FX_LOGS(ERROR) << "summary_entry.version     [0x" << std::hex << summary_entry.version << "]";
    FX_LOGS(ERROR) << "summary_entry.ofs_in_node [0x" << std::hex
                   << LeToCpu(summary_entry.ofs_in_node) << "]";

    FX_LOGS(ERROR) << "parent nid                [0x" << std::hex << parent_nid << "]";
    FX_LOGS(ERROR) << "version from nat          [0x" << std::hex << version << "]";
    FX_LOGS(ERROR) << "index in parent node        [0x" << std::hex << index_in_node << "]";

    FX_LOGS(ERROR) << "Target data block address    [0x" << std::hex << block_address << "]";
    FX_LOGS(ERROR) << "Invalid data seg summary\n";
    ZX_ASSERT(0);
  }
  return true;
}

zx_status_t FsckWorker::CheckNodeBlock(Inode *inode, nid_t nid, FileType ftype, NodeType ntype,
                                       uint32_t &block_count) {
  Node *node_block = nullptr;
  zx_status_t ret = ZX_OK;

  IsValidNid(nid);

  if (ftype != FileType::kFtOrphan || TestValidBitmap(nid, fsck_.nat_area_bitmap.get()) != 0x0) {
    ClearValidBitmap(nid, fsck_.nat_area_bitmap.get());
  } else {
    FX_LOGS(ERROR) << "nid duplicated [0x" << std::hex << nid << "]";
  }

  auto result = GetNodeInfo(nid);
  ZX_ASSERT(result.is_ok());
  NodeInfo node_info = *result;

  // Is it reserved block?
  // if block addresss was kNewAddr
  // it means that block was already allocated, but not stored in disk
  if (node_info.blk_addr == kNewAddr) {
    ++fsck_.result.valid_block_count;
    ++fsck_.result.valid_node_count;
    if (ntype == NodeType::kTypeInode) {
      ++fsck_.result.valid_inode_count;
    }
    return ZX_OK;
  }

  IsValidBlockAddress(node_info.blk_addr);
  IsValidSsaNodeBlock(nid, node_info.blk_addr);

  if (TestValidBitmap(BlkoffFromMain(*segment_manager_, node_info.blk_addr),
                      fsck_.sit_area_bitmap.get()) == 0x0) {
    FX_LOGS(INFO) << "SIT bitmap is 0x0. block_address[0x" << std::hex << node_info.blk_addr << "]";
    ZX_ASSERT(0);
  }

  if (TestValidBitmap(BlkoffFromMain(*segment_manager_, node_info.blk_addr),
                      fsck_.main_area_bitmap.get()) == 0x0) {
    ++fsck_.result.valid_block_count;
    ++fsck_.result.valid_node_count;
  }

  FsBlock fs_block;
  ZX_ASSERT(ReadBlock(fs_block, node_info.blk_addr) == ZX_OK);
#ifdef __Fuchsia__
  node_block = reinterpret_cast<Node *>(fs_block.GetData().data());
#else   // __Fuchsia__
  node_block = reinterpret_cast<Node *>(fs_block.GetData());
#endif  // __Fuchsia__
  ZX_ASSERT_MSG(nid == LeToCpu(node_block->footer.nid),
                "nid[0x%x] blk_addr[0x%x] footer.nid[0x%x]\n", nid, node_info.blk_addr,
                LeToCpu(node_block->footer.nid));

  if (ntype == NodeType::kTypeInode) {
    ret = CheckInodeBlock(nid, ftype, *node_block, block_count, node_info);
  } else {
    // it's not inode
    ZX_ASSERT(node_block->footer.nid != node_block->footer.ino);

    if (TestValidBitmap(BlkoffFromMain(*segment_manager_, node_info.blk_addr),
                        fsck_.main_area_bitmap.get()) != 0) {
      FX_LOGS(INFO) << "Duplicated node block. ino[0x" << std::hex << nid << "][0x" << std::hex
                    << node_info.blk_addr;
      ZX_ASSERT(0);
    }
    SetValidBitmap(BlkoffFromMain(*segment_manager_, node_info.blk_addr),
                   fsck_.main_area_bitmap.get());

    switch (ntype) {
      case NodeType::kTypeDirectNode:
        CheckDnodeBlock(inode, nid, ftype, *node_block, block_count, node_info);
        break;
      case NodeType::kTypeIndirectNode:
        CheckIndirectNodeBlock(inode, nid, ftype, *node_block, block_count);
        break;
      case NodeType::kTypeDoubleIndirectNode:
        CheckDoubleIndirectNodeBlock(inode, nid, ftype, *node_block, block_count);
        break;
      default:
        ZX_ASSERT(0);
    }
  }

  ZX_ASSERT(ret == ZX_OK);

  return ZX_OK;
}

zx_status_t FsckWorker::CheckInodeBlock(nid_t nid, FileType ftype, Node &node_block,
                                        uint32_t &block_count, NodeInfo &node_info) {
  uint32_t child_count = 0, child_files = 0;
  NodeType ntype;
  uint32_t i_links = LeToCpu(node_block.i.i_links);
  uint64_t i_blocks = LeToCpu(node_block.i.i_blocks);

  ZX_ASSERT(node_block.footer.nid == node_block.footer.ino);
  ZX_ASSERT(LeToCpu(node_block.footer.nid) == nid);

  if (TestValidBitmap(BlkoffFromMain(*segment_manager_, node_info.blk_addr),
                      fsck_.main_area_bitmap.get()) == 0x0) {
    ++fsck_.result.valid_inode_count;
  }

  // Orphan node. i_links should be 0
  if (ftype == FileType::kFtOrphan) {
    ZX_ASSERT(i_links == 0);
  } else {
    ZX_ASSERT(i_links > 0);
  }

  if (ftype == FileType::kFtDir) {
    // not included '.' & '..'
    if (TestValidBitmap(BlkoffFromMain(*segment_manager_, node_info.blk_addr),
                        fsck_.main_area_bitmap.get()) != 0) {
      FX_LOGS(INFO) << "Duplicated inode blk. ino[0x" << std::hex << nid << "][0x" << std::hex
                    << node_info.blk_addr;
      ZX_ASSERT(0);
    }
    SetValidBitmap(BlkoffFromMain(*segment_manager_, node_info.blk_addr),
                   fsck_.main_area_bitmap.get());

  } else {
    if (TestValidBitmap(BlkoffFromMain(*segment_manager_, node_info.blk_addr),
                        fsck_.main_area_bitmap.get()) == 0x0) {
      SetValidBitmap(BlkoffFromMain(*segment_manager_, node_info.blk_addr),
                     fsck_.main_area_bitmap.get());
      if (i_links > 1) {
        // First time. Create new hard link node
        AddIntoHardLinkMap(nid, i_links);
        ++fsck_.result.multi_hard_link_files;
      }
    } else {
      if (i_links <= 1) {
        FX_LOGS(ERROR) << "Error. Node ID [0x" << std::hex << nid << "].";
        FX_LOGS(ERROR) << " There are one more hard links. But i_links is [0x" << std::hex
                       << i_links << "].";
        ZX_ASSERT(0);
      }

      FX_LOGS(INFO) << "ino[0x" << std::hex << nid << "] has hard links [0x" << std::hex << i_links
                    << "]";
      zx_status_t status = FindAndDecreaseHardLinkMap(nid);
      ZX_ASSERT(status == ZX_OK);

      // No need to go deep into the node
      return ZX_OK;
    }
  }
#if 0  // porting needed
  fsck_chk_xattr_blk(sbi, nid, LeToCpu(node_block->i.i_xattr_nid), block_count);
#endif

  do {
    if (ftype == FileType::kFtChrdev || ftype == FileType::kFtBlkdev ||
        ftype == FileType::kFtFifo || ftype == FileType::kFtSock) {
      break;
    }
#if 0  // porting needed
  if ((node_block->i.i_inline & F2FS_INLINE_DATA)) {
    FX_LOGS(INFO) << "ino[0x" << std::hex << nid << "] has inline data";
    break;
  }
#endif

    uint16_t base =
        (node_block.i.i_inline & kExtraAttr) ? node_block.i.i_extra_isize / sizeof(uint32_t) : 0;

    if (node_block.i.i_inline & kInlineDentry) {
      uint32_t max_data =
          sizeof(uint32_t) *
          ((kAddrsPerInode - base * sizeof(uint32_t)) / sizeof(uint32_t) - kInlineXattrAddrs - 1);
      uint32_t max_dentry =
          max_data * kBitsPerByte / ((kSizeOfDirEntry + kDentrySlotLen) * kBitsPerByte + 1);

      const auto &entry = reinterpret_cast<const InlineDentry &>(node_block.i.i_addr[base + 1]);

      CheckDentries(child_count, child_files, 1, entry.dentry_bitmap, entry.dentry, entry.filename,
                    max_dentry);
    } else {
      // check data blocks in inode
      for (uint16_t index = base; index < AddrsPerInode(&node_block.i); ++index) {
        if (LeToCpu(node_block.i.i_addr[index]) != 0) {
          ++block_count;
          zx_status_t ret = CheckDataBlock(&node_block.i, LeToCpu(node_block.i.i_addr[index]),
                                           child_count, child_files, (i_blocks == block_count),
                                           ftype, nid, index, node_info.version);
          ZX_ASSERT(ret == ZX_OK);
        }
      }
    }

    // check node blocks in inode: direct(2) + indirect(2) + double indirect(1)
    for (int index = 0; index < 5; ++index) {
      if (index == 0 || index == 1) {
        ntype = NodeType::kTypeDirectNode;
      } else if (index == 2 || index == 3) {
        ntype = NodeType::kTypeIndirectNode;
      } else if (index == 4) {
        ntype = NodeType::kTypeDoubleIndirectNode;
      } else {
        ZX_ASSERT(0);
      }

      if (LeToCpu(node_block.i.i_nid[index]) != 0) {
        ++block_count;
        zx_status_t ret = CheckNodeBlock(&node_block.i, LeToCpu(node_block.i.i_nid[index]), ftype,
                                         ntype, block_count);
        ZX_ASSERT(ret == ZX_OK);
      }
    }
  } while (0);
#if 0  // TODO: implement debug level
  if (ftype == FileType::kFtDir)  // TODO: DBG(1)
    printf("Directory Inode: ino: %x name: %s depth: %d child files: %d\n\n",
           LeToCpu(node_blk->footer.ino), node_blk->i.i_name, LeToCpu(node_blk->i.i_current_depth),
           child_files);
  if (ftype == FileType::kFtOrphan)  // TODO: DBG (1)
    printf("Orphan Inode: ino: %x name: %s i_blocks: %u\n\n", LeToCpu(node_blk->footer.ino),
           node_blk->i.i_name, (uint32_t)i_blocks);
#endif

  if ((ftype == FileType::kFtDir && i_links != child_count) || (i_blocks != block_count)) {
    PrintNodeInfo(node_block);
  }
#if 0  // TODO: implement debug level
  // TODO: DBG (1)
  printf("blk   cnt [0x%x]\n", *blk_cnt);
  // TODO: DBG (1)
  printf("child cnt [0x%x]\n", child_cnt);
#endif

  ZX_ASSERT(i_blocks == block_count);
  if (ftype == FileType::kFtDir) {
    ZX_ASSERT(i_links == child_count);
  }
  return ZX_OK;
}

void FsckWorker::CheckDnodeBlock(Inode *inode, nid_t nid, FileType ftype, Node &node_block,
                                 uint32_t &block_count, NodeInfo &node_info) {
  uint32_t child_count = 0, child_files = 0;
  for (uint16_t index = 0; index < kAddrsPerBlock; ++index) {
    if (LeToCpu(node_block.dn.addr[index]) == 0x0) {
      continue;
    }
    ++block_count;
    CheckDataBlock(inode, LeToCpu(node_block.dn.addr[index]), child_count, child_files,
                   LeToCpu(inode->i_blocks) == block_count, ftype, nid, index, node_info.version);
  }
}

void FsckWorker::CheckIndirectNodeBlock(Inode *inode, nid_t nid, FileType ftype, Node &node_block,
                                        uint32_t &block_count) {
  for (uint32_t i = 0; i < kNidsPerBlock; i++) {
    if (LeToCpu(node_block.in.nid[i]) == 0x0) {
      continue;
    }
    ++block_count;
    CheckNodeBlock(inode, LeToCpu(node_block.in.nid[i]), ftype, NodeType::kTypeDirectNode,
                   block_count);
  }
}

void FsckWorker::CheckDoubleIndirectNodeBlock(Inode *inode, nid_t nid, FileType ftype,
                                              Node &node_block, uint32_t &block_count) {
  for (int i = 0; i < kNidsPerBlock; i++) {
    if (LeToCpu(node_block.in.nid[i]) == 0x0) {
      continue;
    }
    ++block_count;
    CheckNodeBlock(inode, LeToCpu(node_block.in.nid[i]), ftype, NodeType::kTypeIndirectNode,
                   block_count);
  }
}

template <size_t size>
void FsckWorker::PrintDentry(const uint32_t depth, const std::string_view name,
                             const uint8_t (&dentry_bitmap)[size], const DirEntry &dentries,
                             const int index, const int last_block, const int max_entries) {
  int last_de = 0;
  int next_idx = 0;
  int name_len;
  int bit_offset;

#if 0  // porting needed
  if (config.dbg_lv != -1)
    return;
#endif

  name_len = LeToCpu(dentries.name_len);
  next_idx = index + (name_len + kDentrySlotLen - 1) / kDentrySlotLen;

  bit_offset = FindNextBit(dentry_bitmap, max_entries, next_idx);
  if (bit_offset >= max_entries && last_block) {
    last_de = 1;
  }

  if (tree_mark_.size() <= depth) {
    tree_mark_.resize(tree_mark_.size() * 2, 0);
  }
  if (last_de) {
    tree_mark_[depth] = '`';
  } else {
    tree_mark_[depth] = '|';
  }

  if (tree_mark_[depth - 1] == '`') {
    tree_mark_[depth - 1] = ' ';
  }

  for (uint32_t i = 1; i < depth; i++) {
    std::cout << tree_mark_[i] << "   ";
  }
  std::cout << (last_de ? "`" : "|") << "-- " << name << std::endl;
}

template <size_t bitmap_size, size_t entry_size>
void FsckWorker::CheckDentries(uint32_t &child_count, uint32_t &child_files, const int last_block,
                               const uint8_t (&dentry_bitmap)[bitmap_size],
                               const DirEntry (&dentries)[entry_size],
                               const uint8_t (*filename)[kNameLen], const int max_entries) {
  int num_entries = 0;
  uint32_t hash_code;
  uint32_t block_count;
  FileType ftype;

  ++fsck_.dentry_depth;

  for (int i = 0; i < max_entries;) {
    if (TestBit(i, dentry_bitmap) == 0x0) {
      ++i;
      continue;
    }

    std::string_view name(reinterpret_cast<const char *>(filename[i]),
                          LeToCpu(dentries[i].name_len));
    hash_code = DentryHash(name.data(), static_cast<int>(name.length()));

    ftype = static_cast<FileType>(dentries[i].file_type);

    // Becareful. 'dentry.file_type' is not imode
    if (ftype == FileType::kFtDir) {
      ++child_count;
      if (name.compare("..") == 0 || name.compare(".") == 0) {
        ++i;
        continue;
      }
    }

    // TODO: Should we check '.' and '..' entries?
    ZX_ASSERT(LeToCpu(dentries[i].hash_code) == hash_code);
#if 0  // TODO: implement debug level
    // TODO: DBG (2)
    printf("[%3u] - no[0x%x] name[%s] len[0x%x] ino[0x%x] type[0x%x]\n", fsck->dentry_depth, i,
           name.data(), LeToCpu(dentries[i].name_len), LeToCpu(dentries[i].ino),
           dentries[i].file_type);
#endif

    PrintDentry(fsck_.dentry_depth, name, dentry_bitmap, dentries[i], i, last_block, max_entries);

    block_count = 1;
    ZX_ASSERT(CheckNodeBlock(nullptr, LeToCpu(dentries[i].ino), ftype, NodeType::kTypeInode,
                             block_count) == ZX_OK);

    i += (name.length() + kDentrySlotLen - 1) / kDentrySlotLen;
    ++num_entries;
    ++child_files;
  }
#if 0  // TODO: implement debug level
  // TODO: DBG (1)
  printf("[%3d] Dentry Block [0x%x] Done : dentries:%d in %d slots (len:%d)\n\n",
         fsck->dentry_depth, blk_addr, num_entries, kNrDentryInBlock, kMaxNameLen);
#endif

  fsck_.dentry_depth--;
}

void FsckWorker::CheckDentryBlock(uint32_t block_address, uint32_t &child_count,
                                  uint32_t &child_files, int last_block) {
  DentryBlock *de_blk;

  FsBlock fs_block;
  ZX_ASSERT(ReadBlock(fs_block, block_address) == ZX_OK);
#ifdef __Fuchsia__
  de_blk = reinterpret_cast<DentryBlock *>(fs_block.GetData().data());
#else   // __Fuchsia__
  de_blk = reinterpret_cast<DentryBlock *>(fs_block.GetData());
#endif  // __Fuchsia__

  CheckDentries(child_count, child_files, last_block, de_blk->dentry_bitmap, de_blk->dentry,
                de_blk->filename, kNrDentryInBlock);
}

zx_status_t FsckWorker::CheckDataBlock(Inode *inode, uint32_t block_address, uint32_t &child_count,
                                       uint32_t &child_files, int last_block, FileType ftype,
                                       uint32_t parent_nid, uint16_t index_in_node, uint8_t ver) {
  // Is it reserved block?
  if (block_address == kNewAddr) {
    ++fsck_.result.valid_block_count;
    return ZX_OK;
  }

  IsValidBlockAddress(block_address);

  IsValidSsaDataBlock(block_address, parent_nid, index_in_node, ver);

  if (TestValidBitmap(BlkoffFromMain(*segment_manager_, block_address),
                      fsck_.sit_area_bitmap.get()) == 0x0) {
    ZX_ASSERT_MSG(0, "SIT bitmap is 0x0. block_address[0x%x]\n", block_address);
  }

  if (TestValidBitmap(BlkoffFromMain(*segment_manager_, block_address),
                      fsck_.main_area_bitmap.get()) != 0) {
    ZX_ASSERT_MSG(0, "Duplicated data block. pnid[0x%x] index[0x%x] block_address[0x%x]\n",
                  parent_nid, index_in_node, block_address);
  }
  SetValidBitmap(BlkoffFromMain(*segment_manager_, block_address), fsck_.main_area_bitmap.get());

  ++fsck_.result.valid_block_count;

  if (ftype == FileType::kFtDir) {
    CheckDentryBlock(block_address, child_count, child_files, last_block);
  }

  return ZX_OK;
}

void FsckWorker::CheckOrphanNode() {
  uint32_t block_count = 0;
  block_t start_blk, orphan_blkaddr;
  FsBlock fs_block;

  if (!IsSetCkptFlags(&superblock_info_.GetCheckpoint(), kCpOrphanPresentFlag)) {
    return;
  }

  start_blk = superblock_info_.StartCpAddr() + 1;
  orphan_blkaddr = superblock_info_.StartSumAddr() - 1;

  for (block_t i = 0; i < orphan_blkaddr; i++) {
    ZX_ASSERT(ReadBlock(fs_block, start_blk + i) == ZX_OK);
#ifdef __Fuchsia__
    OrphanBlock *orphan_block = reinterpret_cast<OrphanBlock *>(fs_block.GetData().data());
#else   // __Fuchsia__
    OrphanBlock *orphan_block = reinterpret_cast<OrphanBlock *>(fs_block.GetData());
#endif  // __Fuchsia__

    for (block_t j = 0; j < LeToCpu(orphan_block->entry_count); j++) {
      nid_t ino = LeToCpu(orphan_block->ino[j]);
#if 0  // TODO: implement debug level
      // TODO: DBG (1)
      printf("[%3d] ino [0x%x]\n", i, ino);
#endif

      block_count = 1;
      zx_status_t ret =
          CheckNodeBlock(nullptr, ino, FileType::kFtOrphan, NodeType::kTypeInode, block_count);
      ZX_ASSERT(ret == ZX_OK);
    }
  }
}

#if 0  // porting needed
int FsckWorker::FsckChkXattrBlk(uint32_t ino, uint32_t x_nid, uint32_t *block_count) {
  FsckInfo *fsck = &fsck_;
  NodeInfo ni;

  if (x_nid == 0x0)
    return 0;

  if (TestValidBitmap(x_nid, fsck->nat_area_bitmap) != 0x0) {
    ClearValidBitmap(x_nid, fsck->nat_area_bitmap);
  } else {
    ZX_ASSERT_MSG(0, "xattr_nid duplicated [0x%x]\n", x_nid);
  }

  *block_count = *block_count + 1;
  fsck->chk.valid_block_count++;
  fsck->chk.valid_node_count++;

  ZX_ASSERT(GetNodeInfo(x_nid, &ni) >= 0);

  if (TestValidBitmap(BlkoffFromMain(superblock_info, ni.blk_addr), fsck->main_area_bitmap) != 0) {
    ZX_ASSERT_MSG(0,
                  "Duplicated node block for x_attr. "
                  "x_nid[0x%x] block addr[0x%x]\n",
                  x_nid, ni.blk_addr);
  }
  SetValidBitmap(BlkoffFromMain(superblock_info, ni.blk_addr), fsck->main_area_bitmap);
#if 0  // TODO: implement debug level
  // TODO: DBG (2)
  printf("ino[0x%x] x_nid[0x%x]\n", ino, x_nid);
#endif
  return 0;
}
#endif

zx_status_t FsckWorker::Init() {
  fsck_.nr_main_blocks = segment_manager_->GetMainSegmentsCount()
                         << superblock_info_.GetLogBlocksPerSeg();
  fsck_.main_area_bitmap_size = (fsck_.nr_main_blocks + kBitsPerByte - 1) / kBitsPerByte;
  fsck_.main_area_bitmap.reset(new uint8_t[fsck_.main_area_bitmap_size]());
  if (fsck_.main_area_bitmap == nullptr) {
    return ZX_ERR_NO_MEMORY;
  }

  BuildNatAreaBitmap();
  BuildSitAreaBitmap();

  return ZX_OK;
}

zx_status_t FsckWorker::Verify() {
  zx_status_t ret = ZX_OK;
  uint32_t nr_unref_nid = 0;

  printf("\n");

  for (uint32_t i = 0; i < fsck_.nr_nat_entries; i++) {
    if (TestValidBitmap(i, fsck_.nat_area_bitmap.get()) != 0) {
      printf("NID[0x%x] is unreachable\n", i);
      ++nr_unref_nid;
    }
  }

  for (auto const [nid, links] : fsck_.hard_link_map) {
    printf("NID[0x%x] has [0x%x] more unreachable links\n", nid, links);
  }

  printf("[FSCK] Unreachable nat entries                       ");
  if (nr_unref_nid == 0x0) {
    printf(" [Ok..] [0x%x]\n", nr_unref_nid);
  } else {
    printf(" [Fail] [0x%x]\n", nr_unref_nid);
    ret = ZX_ERR_BAD_STATE;
  }

  printf("[FSCK] SIT valid block bitmap checking                ");
  if (memcmp(fsck_.sit_area_bitmap.get(), fsck_.main_area_bitmap.get(),
             fsck_.sit_area_bitmap_size) == 0x0) {
    printf("[Ok..]\n");
  } else {
    printf("[Fail]\n");
    ret = ZX_ERR_BAD_STATE;
  }

  printf("[FSCK] Hard link checking for regular file           ");
  if (fsck_.hard_link_map.empty()) {
    printf(" [Ok..] [0x%x]\n", fsck_.result.multi_hard_link_files);
  } else {
    printf(" [Fail] [0x%x]\n", fsck_.result.multi_hard_link_files);
    ret = ZX_ERR_BAD_STATE;
  }

  printf("[FSCK] valid_block_count matching with CP            ");
  if (superblock_info_.GetTotalValidBlockCount() == fsck_.result.valid_block_count) {
    printf(" [Ok..] [0x%x]\n", (uint32_t)fsck_.result.valid_block_count);
  } else {
    printf(" [Fail] [0x%x]\n", (uint32_t)fsck_.result.valid_block_count);
    ret = ZX_ERR_BAD_STATE;
  }

  printf("[FSCK] valid_node_count matcing with CP (de lookup)  ");
  if (superblock_info_.GetTotalValidNodeCount() == fsck_.result.valid_node_count) {
    printf(" [Ok..] [0x%x]\n", fsck_.result.valid_node_count);
  } else {
    printf(" [Fail] [0x%x]\n", fsck_.result.valid_node_count);
    ret = ZX_ERR_BAD_STATE;
  }

  printf("[FSCK] valid_node_count matcing with CP (nat lookup) ");
  if (superblock_info_.GetTotalValidNodeCount() == fsck_.result.valid_nat_entry_count) {
    printf(" [Ok..] [0x%x]\n", fsck_.result.valid_nat_entry_count);
  } else {
    printf(" [Fail] [0x%x]\n", fsck_.result.valid_nat_entry_count);
    ret = ZX_ERR_BAD_STATE;
  }

  printf("[FSCK] valid_inode_count matched with CP             ");
  if (superblock_info_.GetTotalValidInodeCount() == fsck_.result.valid_inode_count) {
    printf(" [Ok..] [0x%x]\n", fsck_.result.valid_inode_count);
  } else {
    printf(" [Fail] [0x%x]\n", fsck_.result.valid_inode_count);
    ret = ZX_ERR_BAD_STATE;
  }

  return ret;
}

void FsckWorker::PrintInodeInfo(Inode &inode) {
  int namelen = LeToCpu(inode.i_namelen);

  DisplayMember(sizeof(uint32_t), inode.i_mode, "i_mode");
  DisplayMember(sizeof(uint32_t), inode.i_uid, "i_uid");
  DisplayMember(sizeof(uint32_t), inode.i_gid, "i_gid");
  DisplayMember(sizeof(uint32_t), inode.i_links, "i_links");
  DisplayMember(sizeof(uint64_t), inode.i_size, "i_size");
  DisplayMember(sizeof(uint64_t), inode.i_blocks, "i_blocks");

  DisplayMember(sizeof(uint64_t), inode.i_atime, "i_atime");
  DisplayMember(sizeof(uint32_t), inode.i_atime_nsec, "i_atime_nsec");
  DisplayMember(sizeof(uint64_t), inode.i_ctime, "i_ctime");
  DisplayMember(sizeof(uint32_t), inode.i_ctime_nsec, "i_ctime_nsec");
  DisplayMember(sizeof(uint64_t), inode.i_mtime, "i_mtime");
  DisplayMember(sizeof(uint32_t), inode.i_mtime_nsec, "i_mtime_nsec");

  DisplayMember(sizeof(uint32_t), inode.i_generation, "i_generation");
  DisplayMember(sizeof(uint32_t), inode.i_current_depth, "i_current_depth");
  DisplayMember(sizeof(uint32_t), inode.i_xattr_nid, "i_xattr_nid");
  DisplayMember(sizeof(uint32_t), inode.i_flags, "i_flags");
  DisplayMember(sizeof(uint32_t), inode.i_pino, "i_pino");

  if (namelen) {
    DisplayMember(sizeof(uint32_t), inode.i_namelen, "i_namelen");
    inode.i_name[namelen] = '\0';
    DisplayMember(sizeof(char), inode.i_name, "i_name");
  }

  printf("i_ext: fofs:%x blkaddr:%x len:%x\n", inode.i_ext.fofs, inode.i_ext.blk_addr,
         inode.i_ext.len);

  DisplayMember(sizeof(uint32_t), inode.i_addr[0], "i_addr[0]");  // Pointers to data blocks
  DisplayMember(sizeof(uint32_t), inode.i_addr[1], "i_addr[1]");  // Pointers to data blocks
  DisplayMember(sizeof(uint32_t), inode.i_addr[2], "i_addr[2]");  // Pointers to data blocks
  DisplayMember(sizeof(uint32_t), inode.i_addr[3], "i_addr[3]");  // Pointers to data blocks

  for (uint32_t i = 4; i < AddrsPerInode(&inode); i++) {
    if (inode.i_addr[i] != 0x0) {
      printf("i_addr[0x%x] points data block\r\t\t\t\t[0x%4x]\n", i, inode.i_addr[i]);
      break;
    }
  }

  DisplayMember(sizeof(uint32_t), inode.i_nid[0], "i_nid[0]");  // direct
  DisplayMember(sizeof(uint32_t), inode.i_nid[1], "i_nid[1]");  // direct
  DisplayMember(sizeof(uint32_t), inode.i_nid[2], "i_nid[2]");  // indirect
  DisplayMember(sizeof(uint32_t), inode.i_nid[3], "i_nid[3]");  // indirect
  DisplayMember(sizeof(uint32_t), inode.i_nid[4], "i_nid[4]");  // double indirect

  printf("\n");
}

void FsckWorker::PrintNodeInfo(Node &node_block) {
  nid_t ino = LeToCpu(node_block.footer.ino);
  nid_t nid = LeToCpu(node_block.footer.nid);
  if (ino == nid) {
    FX_LOGS(INFO) << "Node ID [0x" << std::hex << nid << ":" << nid << "] is inode";
    PrintInodeInfo(node_block.i);
  } else {
    uint32_t *dump_blk = (uint32_t *)&node_block;
    FX_LOGS(INFO) << "Node ID [0x" << std::hex << nid << ":" << nid
                  << "] is direct node or indirect node";
    for (int i = 0; i <= 10; i++) {  // MSG (0)
      printf("[%d]\t\t\t[0x%8x : %d]\n", i, dump_blk[i], dump_blk[i]);
    }
  }
}

void FsckWorker::PrintRawSuperblockInfo() {
  const Superblock &sb = superblock_info_.GetRawSuperblock();
#if 0  // porting needed
  if (!config.dbg_lv)
    return;
#endif

  printf("\n");
  printf("+--------------------------------------------------------+\n");
  printf("| Super block                                            |\n");
  printf("+--------------------------------------------------------+\n");

  DisplayMember(sizeof(uint32_t), sb.magic, "magic");
  DisplayMember(sizeof(uint32_t), sb.major_ver, "major_ver");
  DisplayMember(sizeof(uint32_t), sb.minor_ver, "minor_ver");
  DisplayMember(sizeof(uint32_t), sb.log_sectorsize, "log_sectorsize");
  DisplayMember(sizeof(uint32_t), sb.log_sectors_per_block, "log_sectors_per_block");

  DisplayMember(sizeof(uint32_t), sb.log_blocksize, "log_blocksize");
  DisplayMember(sizeof(uint32_t), sb.log_blocks_per_seg, "log_blocks_per_seg");
  DisplayMember(sizeof(uint32_t), sb.segs_per_sec, "segs_per_sec");
  DisplayMember(sizeof(uint32_t), sb.secs_per_zone, "secs_per_zone");
  DisplayMember(sizeof(uint32_t), sb.checksum_offset, "checksum_offset");
  DisplayMember(sizeof(uint64_t), sb.block_count, "block_count");

  DisplayMember(sizeof(uint32_t), sb.section_count, "section_count");
  DisplayMember(sizeof(uint32_t), sb.segment_count, "segment_count");
  DisplayMember(sizeof(uint32_t), sb.segment_count_ckpt, "segment_count_ckpt");
  DisplayMember(sizeof(uint32_t), sb.segment_count_sit, "segment_count_sit");
  DisplayMember(sizeof(uint32_t), sb.segment_count_nat, "segment_count_nat");

  DisplayMember(sizeof(uint32_t), sb.segment_count_ssa, "segment_count_ssa");
  DisplayMember(sizeof(uint32_t), sb.segment_count_main, "segment_count_main");
  DisplayMember(sizeof(uint32_t), sb.segment0_blkaddr, "segment0_blkaddr");

  DisplayMember(sizeof(uint32_t), sb.cp_blkaddr, "cp_blkaddr");
  DisplayMember(sizeof(uint32_t), sb.sit_blkaddr, "sit_blkaddr");
  DisplayMember(sizeof(uint32_t), sb.nat_blkaddr, "nat_blkaddr");
  DisplayMember(sizeof(uint32_t), sb.ssa_blkaddr, "ssa_blkaddr");
  DisplayMember(sizeof(uint32_t), sb.main_blkaddr, "main_blkaddr");

  DisplayMember(sizeof(uint32_t), sb.root_ino, "root_ino");
  DisplayMember(sizeof(uint32_t), sb.node_ino, "node_ino");
  DisplayMember(sizeof(uint32_t), sb.meta_ino, "meta_ino");
  printf("\n");
}

void FsckWorker::PrintCheckpointInfo() {
  Checkpoint &cp = superblock_info_.GetCheckpoint();
  uint32_t alloc_type;
#if 0  // porting needed
  if (!config.dbg_lv)
    return;
#endif

  printf("\n");
  printf("+--------------------------------------------------------+\n");
  printf("| Checkpoint                                             |\n");
  printf("+--------------------------------------------------------+\n");

  DisplayMember(sizeof(uint64_t), cp.checkpoint_ver, "checkpoint_ver");
  DisplayMember(sizeof(uint64_t), cp.user_block_count, "user_block_count");
  DisplayMember(sizeof(uint64_t), cp.valid_block_count, "valid_block_count");
  DisplayMember(sizeof(uint32_t), cp.rsvd_segment_count, "rsvd_segment_count");
  DisplayMember(sizeof(uint32_t), cp.overprov_segment_count, "overprov_segment_count");
  DisplayMember(sizeof(uint32_t), cp.free_segment_count, "free_segment_count");

  alloc_type = cp.alloc_type[static_cast<int>(CursegType::kCursegHotNode)];
  DisplayMember(sizeof(uint32_t), alloc_type, "alloc_type[CursegType::kCursegHotNode]");
  alloc_type = cp.alloc_type[static_cast<int>(CursegType::kCursegWarmNode)];
  DisplayMember(sizeof(uint32_t), alloc_type, "alloc_type[CursegType::kCursegWarmNode]");
  alloc_type = cp.alloc_type[static_cast<int>(CursegType::kCursegColdNode)];
  DisplayMember(sizeof(uint32_t), alloc_type, "alloc_type[CursegType::kCursegColdNode]");
  alloc_type = cp.alloc_type[static_cast<int>(CursegType::kCursegHotNode)];
  DisplayMember(sizeof(uint32_t), cp.cur_node_segno[0], "cur_node_segno[0]");
  DisplayMember(sizeof(uint32_t), cp.cur_node_segno[1], "cur_node_segno[1]");
  DisplayMember(sizeof(uint32_t), cp.cur_node_segno[2], "cur_node_segno[2]");

  DisplayMember(sizeof(uint32_t), cp.cur_node_blkoff[0], "cur_node_blkoff[0]");
  DisplayMember(sizeof(uint32_t), cp.cur_node_blkoff[1], "cur_node_blkoff[1]");
  DisplayMember(sizeof(uint32_t), cp.cur_node_blkoff[2], "cur_node_blkoff[2]");

  alloc_type = cp.alloc_type[static_cast<int>(CursegType::kCursegHotData)];
  DisplayMember(sizeof(uint32_t), alloc_type, "alloc_type[CursegType::kCursegHotData]");
  alloc_type = cp.alloc_type[static_cast<int>(CursegType::kCursegWarmData)];
  DisplayMember(sizeof(uint32_t), alloc_type, "alloc_type[CursegType::kCursegWarmData]");
  alloc_type = cp.alloc_type[static_cast<int>(CursegType::kCursegColdData)];
  DisplayMember(sizeof(uint32_t), alloc_type, "alloc_type[CursegType::kCursegColdData]");
  DisplayMember(sizeof(uint32_t), cp.cur_data_segno[0], "cur_data_segno[0]");
  DisplayMember(sizeof(uint32_t), cp.cur_data_segno[1], "cur_data_segno[1]");
  DisplayMember(sizeof(uint32_t), cp.cur_data_segno[2], "cur_data_segno[2]");

  DisplayMember(sizeof(uint32_t), cp.cur_data_blkoff[0], "cur_data_blkoff[0]");
  DisplayMember(sizeof(uint32_t), cp.cur_data_blkoff[1], "cur_data_blkoff[1]");
  DisplayMember(sizeof(uint32_t), cp.cur_data_blkoff[2], "cur_data_blkoff[2]");

  DisplayMember(sizeof(uint32_t), cp.ckpt_flags, "ckpt_flags");
  DisplayMember(sizeof(uint32_t), cp.cp_pack_total_block_count, "cp_pack_total_block_count");
  DisplayMember(sizeof(uint32_t), cp.cp_pack_start_sum, "cp_pack_start_sum");
  DisplayMember(sizeof(uint32_t), cp.valid_node_count, "valid_node_count");
  DisplayMember(sizeof(uint32_t), cp.valid_inode_count, "valid_inode_count");
  DisplayMember(sizeof(uint32_t), cp.next_free_nid, "next_free_nid");
  DisplayMember(sizeof(uint32_t), cp.sit_ver_bitmap_bytesize, "sit_ver_bitmap_bytesize");
  DisplayMember(sizeof(uint32_t), cp.nat_ver_bitmap_bytesize, "nat_ver_bitmap_bytesize");
  DisplayMember(sizeof(uint32_t), cp.checksum_offset, "checksum_offset");
  DisplayMember(sizeof(uint64_t), cp.elapsed_time, "elapsed_time");

  printf("\n\n");
}

zx_status_t FsckWorker::SanityCheckRawSuper(const Superblock *raw_super) {
  if (kF2fsSuperMagic != LeToCpu(raw_super->magic)) {
    return ZX_ERR_BAD_STATE;
  }
  if (kBlockSize != kPageCacheSize) {
    return ZX_ERR_BAD_STATE;
  }
  block_t blocksize = 1 << LeToCpu(raw_super->log_blocksize);
  if (kBlockSize != blocksize) {
    return ZX_ERR_BAD_STATE;
  }
  if (LeToCpu(raw_super->log_sectorsize) > kMaxLogSectorSize ||
      LeToCpu(raw_super->log_sectorsize) < kMinLogSectorSize) {
    return ZX_ERR_BAD_STATE;
  }
  if (LeToCpu(raw_super->log_sectors_per_block) + LeToCpu(raw_super->log_sectorsize) !=
      kMaxLogSectorSize) {
    return ZX_ERR_BAD_STATE;
  }
  return ZX_OK;
}

zx_status_t FsckWorker::ValidateSuperblock(block_t block) {
  std::shared_ptr<Superblock> sb(new Superblock());
  zx_status_t ret = ZX_OK;
  if (ret = LoadSuperblock(bc_.get(), sb.get()); ret != ZX_OK) {
    return ret;
  }

  if (ret = SanityCheckRawSuper(sb.get()); ret == ZX_OK) {
    superblock_info_.SetRawSuperblock(sb);
    return ret;
  }
  FX_LOGS(WARNING) << "Can't find a valid F2FS filesystem in" << block << "superblock";
  return ret;
}

void FsckWorker::InitSuperblockInfo() {
  const Superblock &raw_super = superblock_info_.GetRawSuperblock();

  superblock_info_.SetLogSectorsPerBlock(LeToCpu(raw_super.log_sectors_per_block));
  superblock_info_.SetLogBlocksize(LeToCpu(raw_super.log_blocksize));
  superblock_info_.SetBlocksize(1 << superblock_info_.GetLogBlocksize());
  superblock_info_.SetLogBlocksPerSeg(LeToCpu(raw_super.log_blocks_per_seg));
  superblock_info_.SetBlocksPerSeg(1 << superblock_info_.GetLogBlocksPerSeg());
  superblock_info_.SetSegsPerSec(LeToCpu(raw_super.segs_per_sec));
  superblock_info_.SetSecsPerZone(LeToCpu(raw_super.secs_per_zone));
  superblock_info_.SetTotalSections(LeToCpu(raw_super.section_count));
  superblock_info_.SetTotalNodeCount((LeToCpu(raw_super.segment_count_nat) / 2) *
                                     superblock_info_.GetBlocksPerSeg() * kNatEntryPerBlock);
  superblock_info_.SetRootIno(LeToCpu(raw_super.root_ino));
  superblock_info_.SetNodeIno(LeToCpu(raw_super.node_ino));
  superblock_info_.SetMetaIno(LeToCpu(raw_super.meta_ino));
#if 0  // porting needed
  superblock_info_.cur_victim_sec = kNullSegNo;
#endif
}

zx::status<std::pair<std::unique_ptr<FsBlock>, uint64_t>> FsckWorker::ValidateCheckpoint(
    block_t cp_addr) {
  std::unique_ptr<FsBlock> cp_page_1(new FsBlock()), cp_page_2(new FsBlock());
  Checkpoint *cp_block;
  block_t blk_size = superblock_info_.GetBlocksize();
  uint64_t cur_version = 0, pre_version = 0;
  uint32_t crc = 0;
  uint32_t crc_offset;

  // Read the 1st cp block in this CP pack
  if (ReadBlock(*cp_page_1.get(), cp_addr) != ZX_OK) {
    return zx::error(ZX_ERR_BAD_STATE);
  }

  cp_block = (Checkpoint *)cp_page_1.get();
  crc_offset = LeToCpu(cp_block->checksum_offset);
  if (crc_offset >= blk_size) {
    return zx::error(ZX_ERR_BAD_STATE);
  }

  crc = *(uint32_t *)((uint8_t *)cp_block + crc_offset);
  if (!F2fsCrcValid(crc, cp_block, crc_offset)) {
    return zx::error(ZX_ERR_BAD_STATE);
  }

  pre_version = LeToCpu(cp_block->checkpoint_ver);

  // Read the 2nd cp block in this CP pack
  cp_addr += LeToCpu(cp_block->cp_pack_total_block_count) - 1;
  if (ReadBlock(*cp_page_2.get(), cp_addr) != ZX_OK) {
    return zx::error(ZX_ERR_BAD_STATE);
  }

  cp_block = (Checkpoint *)cp_page_2.get();
  crc_offset = LeToCpu(cp_block->checksum_offset);
  if (crc_offset >= blk_size) {
    return zx::error(ZX_ERR_BAD_STATE);
  }

  crc = *(uint32_t *)((uint8_t *)cp_block + crc_offset);
  if (!F2fsCrcValid(crc, cp_block, crc_offset)) {
    return zx::error(ZX_ERR_BAD_STATE);
  }

  cur_version = LeToCpu(cp_block->checkpoint_ver);

  if (cur_version == pre_version) {
    return zx::ok(std::pair<std::unique_ptr<FsBlock>, uint64_t>{std::move(cp_page_1), cur_version});
  }
  return zx::error(ZX_ERR_BAD_STATE);
}

zx_status_t FsckWorker::GetValidCheckpoint() {
  const Superblock &raw_sb = superblock_info_.GetRawSuperblock();
  std::unique_ptr<FsBlock> cur_page;
  uint64_t blk_size = superblock_info_.GetBlocksize();
  block_t cp_start_blk_no;

  // Finding out valid cp block involves read both
  // sets( cp pack1 and cp pack 2)
  cp_start_blk_no = LeToCpu(raw_sb.cp_blkaddr);
  auto cp1 = ValidateCheckpoint(cp_start_blk_no);

  // The second checkpoint pack should start at the next segment
  cp_start_blk_no += 1 << LeToCpu(raw_sb.log_blocks_per_seg);
  auto cp2 = ValidateCheckpoint(cp_start_blk_no);

  if (cp1.is_ok() && cp2.is_ok()) {
    if (VerAfter(cp2->second, cp1->second)) {
      cur_page = std::move(cp2->first);
    } else {
      cur_page = std::move(cp1->first);
    }
  } else if (cp1.is_ok()) {
    cur_page = std::move(cp1->first);
  } else if (cp2.is_ok()) {
    cur_page = std::move(cp2->first);
  } else {
    return ZX_ERR_INVALID_ARGS;
  }

  memcpy(&superblock_info_.GetCheckpoint(), cur_page.get(), blk_size);

  return ZX_OK;
}

zx_status_t FsckWorker::SanityCheckCkpt() {
  uint32_t total, fsmeta;
  const Superblock &raw_super = superblock_info_.GetRawSuperblock();
  Checkpoint &ckpt = superblock_info_.GetCheckpoint();

  total = LeToCpu(raw_super.segment_count);
  fsmeta = LeToCpu(raw_super.segment_count_ckpt);
  fsmeta += LeToCpu(raw_super.segment_count_sit);
  fsmeta += LeToCpu(raw_super.segment_count_nat);
  fsmeta += LeToCpu(ckpt.rsvd_segment_count);
  fsmeta += LeToCpu(raw_super.segment_count_ssa);

  if (fsmeta >= total) {
    return ZX_ERR_INVALID_ARGS;
  }

  return ZX_OK;
}

zx_status_t FsckWorker::InitNodeManager() {
  const Superblock &sb_raw = superblock_info_.GetRawSuperblock();
  uint32_t nat_segs, nat_blocks;

  node_manager_->SetNatAddress(LeToCpu(sb_raw.nat_blkaddr));

  // segment_count_nat includes pair segment so divide to 2.
  nat_segs = LeToCpu(sb_raw.segment_count_nat) >> 1;
  nat_blocks = nat_segs << LeToCpu(sb_raw.log_blocks_per_seg);
  node_manager_->SetMaxNid(kNatEntryPerBlock * nat_blocks);
  node_manager_->SetFirstScanNid(LeToCpu(superblock_info_.GetCheckpoint().next_free_nid));
  node_manager_->SetNextScanNid(LeToCpu(superblock_info_.GetCheckpoint().next_free_nid));
  if (zx_status_t status =
          node_manager_->AllocNatBitmap(superblock_info_.BitmapSize(MetaBitmap::kNatBitmap));
      status != ZX_OK) {
    return ZX_ERR_NO_MEMORY;
  }

  // copy version bitmap
  node_manager_->SetNatBitmap(
      static_cast<uint8_t *>(superblock_info_.BitmapPtr(MetaBitmap::kNatBitmap)));
  return ZX_OK;
}

zx_status_t FsckWorker::BuildNodeManager() {
  if (node_manager_ = std::make_unique<NodeManager>(&superblock_info_); node_manager_ == nullptr) {
    return ZX_ERR_NO_MEMORY;
  }

  if (zx_status_t err = InitNodeManager(); err != ZX_OK) {
    return err;
  }

  return ZX_OK;
}

zx_status_t FsckWorker::BuildSitInfo() {
  const Superblock &raw_sb = superblock_info_.GetRawSuperblock();
  Checkpoint &ckpt = superblock_info_.GetCheckpoint();
  std::unique_ptr<SitInfo> sit_i;
  uint32_t sit_segs;
  uint8_t *src_bitmap;
  uint32_t bitmap_size;

  if (sit_i = std::make_unique<SitInfo>(); sit_i == nullptr) {
    return ZX_ERR_NO_MEMORY;
  }

  sit_i->sentries = new SegmentEntry[segment_manager_->TotalSegs()]();

  for (uint32_t start = 0; start < segment_manager_->TotalSegs(); ++start) {
    sit_i->sentries[start].cur_valid_map = std::make_unique<uint8_t[]>(kSitVBlockMapSize);
    sit_i->sentries[start].ckpt_valid_map = std::make_unique<uint8_t[]>(kSitVBlockMapSize);
    if (sit_i->sentries[start].cur_valid_map == nullptr ||
        sit_i->sentries[start].ckpt_valid_map == nullptr) {
      return ZX_ERR_NO_MEMORY;
    }
  }

  sit_segs = LeToCpu(raw_sb.segment_count_sit) >> 1;
  bitmap_size = superblock_info_.BitmapSize(MetaBitmap::kSitBitmap);
  if (src_bitmap = static_cast<uint8_t *>(superblock_info_.BitmapPtr(MetaBitmap::kSitBitmap));
      src_bitmap == nullptr) {
    return ZX_ERR_NO_MEMORY;
  }

  if (sit_i->sit_bitmap = std::make_unique<uint8_t[]>(bitmap_size); sit_i->sit_bitmap == nullptr) {
    return ZX_ERR_NO_MEMORY;
  }

  memcpy(sit_i->sit_bitmap.get(), src_bitmap, bitmap_size);

  sit_i->sit_base_addr = LeToCpu(raw_sb.sit_blkaddr);
  sit_i->sit_blocks = sit_segs << superblock_info_.GetLogBlocksPerSeg();
  sit_i->written_valid_blocks = LeToCpu(safemath::checked_cast<uint32_t>(ckpt.valid_block_count));
  sit_i->bitmap_size = bitmap_size;
  sit_i->dirty_sentries = 0;
  sit_i->sents_per_block = kSitEntryPerBlock;
  sit_i->elapsed_time = LeToCpu(ckpt.elapsed_time);

  segment_manager_->SetSitInfo(std::move(sit_i));
  return ZX_OK;
}

void FsckWorker::ResetCurseg(CursegType type, int modified) {
  CursegInfo *curseg = segment_manager_->CURSEG_I(type);

  curseg->segno = curseg->next_segno;
  curseg->zone = segment_manager_->GetZoneNoFromSegNo(curseg->segno);
  curseg->next_blkoff = 0;
  curseg->next_segno = kNullSegNo;
}

zx_status_t FsckWorker::ReadCompactedSummaries() {
  Checkpoint &ckpt = superblock_info_.GetCheckpoint();
  block_t start;
  FsBlock fs_block;
  uint32_t offset;
  CursegInfo *curseg;

  start = StartSummaryBlock();

  ReadBlock(fs_block, start++);

  curseg = segment_manager_->CURSEG_I(CursegType::kCursegHotData);
#ifdef __Fuchsia__
  memcpy(&curseg->sum_blk->n_nats, fs_block.GetData().data(), kSumJournalSize);
#else   // __Fuchsia__
  memcpy(&curseg->sum_blk->n_nats, fs_block.GetData(), kSumJournalSize);
#endif  // __Fuchsia__

  curseg = segment_manager_->CURSEG_I(CursegType::kCursegColdData);
#ifdef __Fuchsia__
  memcpy(&curseg->sum_blk->n_sits, fs_block.GetData().data() + kSumJournalSize, kSumJournalSize);
#else   // __Fuchsia__
  memcpy(&curseg->sum_blk->n_sits, fs_block.GetData() + kSumJournalSize, kSumJournalSize);
#endif  // __Fuchsia__

  offset = 2 * kSumJournalSize;
  for (int32_t i = static_cast<int32_t>(CursegType::kCursegHotData);
       i <= CursegType::kCursegColdData; i++) {
    unsigned short blk_off;
    uint32_t segno;

    curseg = segment_manager_->CURSEG_I(static_cast<CursegType>(i));
    segno = LeToCpu(ckpt.cur_data_segno[i]);
    blk_off = LeToCpu(ckpt.cur_data_blkoff[i]);
    curseg->next_segno = segno;
    ResetCurseg(static_cast<CursegType>(i), 0);
    curseg->alloc_type = ckpt.alloc_type[i];
    curseg->next_blkoff = blk_off;

    if (curseg->alloc_type == static_cast<uint8_t>(AllocMode::kSSR)) {
      blk_off = safemath::checked_cast<unsigned short>(superblock_info_.GetBlocksPerSeg());
    }

    for (uint32_t j = 0; j < blk_off; j++) {
      Summary *s;
#ifdef __Fuchsia__
      s = (Summary *)(fs_block.GetData().data() + offset);
#else   // __Fuchsia__
      s = (Summary *)(fs_block.GetData() + offset);
#endif  // __Fuchsia__
      curseg->sum_blk->entries[j] = *s;
      offset += kSummarySize;
      if (offset + kSummarySize <= kPageCacheSize - kSumFooterSize) {
        continue;
      }
#ifdef __Fuchsia__
      memset(fs_block.GetData().data(), 0, kPageSize);
#else   // __Fuchsia__
      memset(fs_block.GetData(), 0, kPageSize);
#endif  // __Fuchsia__
      ReadBlock(fs_block, start++);
      offset = 0;
    }
  }

  return ZX_OK;
}

zx_status_t FsckWorker::RestoreNodeSummary(uint32_t segno, SummaryBlock &summary_block) {
  Node *node_block;
  block_t addr;
  FsBlock fs_block;

  // scan the node segment
  addr = segment_manager_->StartBlock(segno);
  for (uint32_t i = 0; i < superblock_info_.GetBlocksPerSeg(); ++i, ++addr) {
    if (ReadBlock(fs_block, addr)) {
      break;
    }
#ifdef __Fuchsia__
    node_block = reinterpret_cast<Node *>(fs_block.GetData().data());
#else   // __Fuchsia__
    node_block = reinterpret_cast<Node *>(fs_block.GetData());
#endif  // __Fuchsia__
    summary_block.entries[i].nid = node_block->footer.nid;
  }
  return ZX_OK;
}

zx_status_t FsckWorker::ReadNormalSummaries(CursegType type) {
  Checkpoint &ckpt = superblock_info_.GetCheckpoint();
  FsBlock fs_block;
  SummaryBlock *summary_block;
  CursegInfo *curseg;
  unsigned short blk_off;
  uint32_t segno = 0;
  block_t block_address = 0;

  if (segment_manager_->IsDataSeg(type)) {
    segno = LeToCpu(ckpt.cur_data_segno[static_cast<int>(type)]);
    blk_off = LeToCpu(ckpt.cur_data_blkoff[type - CursegType::kCursegHotData]);

    if (IsSetCkptFlags(&ckpt, kCpUmountFlag)) {
      block_address = SummaryBlockAddress(kNrCursegType, static_cast<int>(type));
    } else {
      block_address = SummaryBlockAddress(kNrCursegDataType, static_cast<int>(type));
    }
  } else {
    segno = LeToCpu(ckpt.cur_node_segno[type - CursegType::kCursegHotNode]);
    blk_off = LeToCpu(ckpt.cur_node_blkoff[type - CursegType::kCursegHotNode]);

    if (IsSetCkptFlags(&ckpt, kCpUmountFlag)) {
      block_address = SummaryBlockAddress(kNrCursegNodeType, type - CursegType::kCursegHotNode);
    } else {
      block_address = segment_manager_->GetSumBlock(segno);
    }
  }

  ReadBlock(fs_block, block_address);
#ifdef __Fuchsia__
  summary_block = reinterpret_cast<SummaryBlock *>(fs_block.GetData().data());
#else   // __Fuchsia__
  summary_block = reinterpret_cast<SummaryBlock *>(fs_block.GetData());
#endif  // __Fuchsia__

  if (segment_manager_->IsNodeSeg(type)) {
    if (IsSetCkptFlags(&ckpt, kCpUmountFlag)) {
#if 0  // do not change original value
      Summary *sum_entry = &sum_blk->entries[0];
      for (uint64_t i = 0; i < superblock_info->GetBlocksPerSeg(); i++, sum_entry++) {
				sum_entry->version = 0;
				sum_entry->ofs_in_node = 0;
      }
#endif
    } else {
      if (zx_status_t ret = RestoreNodeSummary(segno, *summary_block); ret != ZX_OK) {
        return ret;
      }
    }
  }

  curseg = segment_manager_->CURSEG_I(type);
  memcpy(curseg->sum_blk, summary_block, kPageCacheSize);
  curseg->next_segno = segno;
  ResetCurseg(type, 0);
  curseg->alloc_type = ckpt.alloc_type[static_cast<int>(type)];
  curseg->next_blkoff = blk_off;

  return ZX_OK;
}

zx_status_t FsckWorker::RestoreCursegSummaries() {
  int32_t type = static_cast<int32_t>(CursegType::kCursegHotData);

  if (IsSetCkptFlags(&superblock_info_.GetCheckpoint(), kCpCompactSumFlag)) {
    if (zx_status_t ret = ReadCompactedSummaries(); ret != ZX_OK) {
      return ret;
    }
    type = static_cast<int32_t>(CursegType::kCursegHotNode);
  }

  for (; type <= CursegType::kCursegColdNode; type++) {
    if (zx_status_t ret = ReadNormalSummaries(static_cast<CursegType>(type)); ret != ZX_OK) {
      return ret;
    }
  }
  return ZX_OK;
}

zx_status_t FsckWorker::BuildCurseg() {
  for (int i = 0; i < kNrCursegType; i++) {
    CursegInfo *curseg = segment_manager_->CURSEG_I(static_cast<CursegType>(i));
    curseg->raw_blk = new FsBlock();
    curseg->segno = kNullSegNo;
    curseg->next_blkoff = 0;
  }
  return RestoreCursegSummaries();
}

inline void FsckWorker::CheckSegmentRange(uint32_t segno) {
  uint32_t end_segno = segment_manager_->GetSegmentsCount() - 1;
  ZX_ASSERT(segno <= end_segno);
}

std::unique_ptr<FsBlock> FsckWorker::GetCurrentSitPage(uint32_t segno) {
  SitInfo &sit_i = segment_manager_->GetSitInfo();
  uint32_t offset = segment_manager_->SitBlockOffset(segno);
  block_t block_address = sit_i.sit_base_addr + offset;
  std::unique_ptr<FsBlock> sit_block(new FsBlock());

  CheckSegmentRange(segno);

  // calculate sit block address
  if (TestValidBitmap(offset, sit_i.sit_bitmap.get())) {
    block_address += sit_i.sit_blocks;
  }

  ReadBlock(*sit_block.get(), block_address);

  return sit_block;
}

void FsckWorker::CheckBlockCount(uint32_t segno, const SitEntry &raw_sit) {
  uint32_t end_segno = segment_manager_->GetSegmentsCount() - 1;
  int valid_blocks = 0;

  // check segment usage
  ZX_ASSERT(GetSitVblocks(raw_sit) <= superblock_info_.GetBlocksPerSeg());

  // check boundary of a given segment number
  ZX_ASSERT(segno <= end_segno);

  // check bitmap with valid block count
  for (uint64_t i = 0; i < superblock_info_.GetBlocksPerSeg(); i++) {
    if (TestValidBitmap(i, raw_sit.valid_map)) {
      ++valid_blocks;
    }
  }
  ZX_ASSERT(GetSitVblocks(raw_sit) == valid_blocks);
}

void FsckWorker::SegmentInfoFromRawSit(SegmentEntry &segment_entry, const SitEntry &raw_sit) {
  segment_entry.valid_blocks = GetSitVblocks(raw_sit);
  segment_entry.ckpt_valid_blocks = GetSitVblocks(raw_sit);
  memcpy(segment_entry.cur_valid_map.get(), raw_sit.valid_map, kSitVBlockMapSize);
  memcpy(segment_entry.ckpt_valid_map.get(), raw_sit.valid_map, kSitVBlockMapSize);
  segment_entry.type = GetSitType(raw_sit);
  segment_entry.mtime = LeToCpu(raw_sit.mtime);
}

SegmentEntry &FsckWorker::GetSegmentEntry(uint32_t segno) {
  SitInfo &sit_i = segment_manager_->GetSitInfo();
  return sit_i.sentries[segno];
}

std::pair<std::unique_ptr<FsBlock>, SegType> FsckWorker::GetSumBlockInfo(uint32_t segno) {
  std::unique_ptr<FsBlock> summary_block(new FsBlock());
  Checkpoint &ckpt = superblock_info_.GetCheckpoint();
  CursegInfo *curseg;
  block_t ssa_blk;

  ssa_blk = segment_manager_->GetSumBlock(segno);
  for (int type = 0; type < kNrCursegNodeType; type++) {
    if (segno == ckpt.cur_node_segno[type]) {
      curseg = segment_manager_->CURSEG_I(CursegType::kCursegHotNode + type);
      memcpy(summary_block.get(), curseg->sum_blk, kBlockSize);
      return {std::move(summary_block),
              SegType::kSegTypeCurNode};  // current node seg was not stored
    }
  }

  for (int type = 0; type < kNrCursegDataType; type++) {
    if (segno == ckpt.cur_data_segno[type]) {
      curseg = segment_manager_->CURSEG_I(CursegType::kCursegHotData + type);
#ifdef __Fuchsia__
      memcpy(summary_block->GetData().data(), curseg->sum_blk, kBlockSize);
#else   // __Fuchsia__
      memcpy(summary_block->GetData(), curseg->sum_blk, kBlockSize);
#endif  // __Fuchsia__
      ZX_ASSERT(!IsSumNodeSeg((reinterpret_cast<SummaryBlock *>(summary_block.get()))->footer));
#if 0  // TODO: implement debug level
      // TODO: DBG (2)
      printf("segno [0x%x] is current data seg[0x%x]\n", segno, type);
#endif

      return {std::move(summary_block),
              SegType::kSegTypeCurData};  // current data seg was not stored
    }
  }

  ZX_ASSERT(ReadBlock(*summary_block.get(), ssa_blk) == ZX_OK);

  if (IsSumNodeSeg((reinterpret_cast<SummaryBlock *>(summary_block.get()))->footer)) {
    return {std::move(summary_block), SegType::kSegTypeNode};
  }
  return {std::move(summary_block), SegType::kSegTypeData};
}

uint32_t FsckWorker::GetSegmentNumber(uint32_t block_address) {
  return (uint32_t)(BlkoffFromMain(*segment_manager_, block_address) >>
                    superblock_info_.GetLogBlocksPerSeg());
}

std::pair<SegType, Summary> FsckWorker::GetSummaryEntry(uint32_t block_address) {
  uint32_t segno, offset;
  Summary summary_entry;

  segno = GetSegmentNumber(block_address);
  offset = OffsetInSegment(superblock_info_, *segment_manager_, block_address);

  auto [summary_block, type] = GetSumBlockInfo(segno);
#ifdef __Fuchsia__
  memcpy(&summary_entry,
         &((reinterpret_cast<SummaryBlock *>(summary_block->GetData().data()))->entries[offset]),
         sizeof(Summary));
#else   // __Fuchsia__
  memcpy(&summary_entry,
         &((reinterpret_cast<SummaryBlock *>(summary_block->GetData()))->entries[offset]),
         sizeof(Summary));
#endif  // __Fuchsia__
  return {type, summary_entry};
}

zx::status<RawNatEntry> FsckWorker::GetNatEntry(nid_t nid) {
  RawNatEntry raw_nat;
  block_t block_off;
  block_t block_addr;
  block_t seg_off;
  int entry_off;

  if ((nid / kNatEntryPerBlock) > fsck_.nr_nat_entries) {
    FX_LOGS(WARNING) << "nid is over max nid";
    return zx::error(ZX_ERR_INVALID_ARGS);
  }

  if (auto result = LookupNatInJournal(nid); result.is_ok()) {
    return result;
  }

  block_off = nid / kNatEntryPerBlock;
  entry_off = nid % kNatEntryPerBlock;

  seg_off = block_off >> superblock_info_.GetLogBlocksPerSeg();
  block_addr =
      (node_manager_->GetNatAddress() + (seg_off << superblock_info_.GetLogBlocksPerSeg() << 1) +
       (block_off & ((1 << superblock_info_.GetLogBlocksPerSeg()) - 1)));

  if (TestValidBitmap(block_off, node_manager_->GetNatBitmap())) {
    block_addr += superblock_info_.GetBlocksPerSeg();
  }

  FsBlock fs_block;
  ZX_ASSERT(ReadBlock(fs_block, block_addr) == ZX_OK);
#ifdef __Fuchsia__
  NatBlock *nat_block = reinterpret_cast<NatBlock *>(fs_block.GetData().data());
#else   // __Fuchsia__
  NatBlock *nat_block = reinterpret_cast<NatBlock *>(fs_block.GetData());
#endif  // __Fuchsia__

  memcpy(&raw_nat, &nat_block->entries[entry_off], sizeof(RawNatEntry));
  return zx::ok(raw_nat);
}

zx::status<NodeInfo> FsckWorker::GetNodeInfo(nid_t nid) {
  NodeInfo node_info;
  auto result = GetNatEntry(nid);
  if (result.is_error()) {
    return zx::error(result.error_value());
  }
  RawNatEntry raw_nat = *result;

  node_info.nid = nid;
  NodeInfoFromRawNat(node_info, raw_nat);
  return zx::ok(node_info);
}

void FsckWorker::BuildSitEntries() {
  SitInfo &sit_i = segment_manager_->GetSitInfo();
  CursegInfo *curseg = segment_manager_->CURSEG_I(CursegType::kCursegColdData);
  SummaryBlock *sum = curseg->sum_blk;

  for (uint32_t segno = 0; segno < segment_manager_->TotalSegs(); segno++) {
    SegmentEntry &segment_entry = sit_i.sentries[segno];
    SitEntry sit;
    bool found = false;

    for (int i = 0; i < SitsInCursum(sum); i++) {
      if (LeToCpu(SegnoInJournal(sum, i)) == segno) {
        sit = sum->sit_j.entries[i].se;
        found = true;
        break;
      }
    }
    if (found == false) {
      std::unique_ptr<FsBlock> sit_block = GetCurrentSitPage(segno);
      sit = reinterpret_cast<SitBlock *>(sit_block.get())
                ->entries[segment_manager_->SitEntryOffset(segno)];
    }
    CheckBlockCount(segno, sit);
    SegmentInfoFromRawSit(segment_entry, sit);
  }
}

zx_status_t FsckWorker::BuildSegmentManager() {
  Superblock &raw_super = superblock_info_.GetRawSuperblock();
  Checkpoint &ckpt = superblock_info_.GetCheckpoint();

  if (segment_manager_ = std::make_unique<SegmentManager>(&superblock_info_);
      segment_manager_ == nullptr) {
    return ZX_ERR_NO_MEMORY;
  }

  // init sm info
  segment_manager_->SetSegment0StartBlock(LeToCpu(raw_super.segment0_blkaddr));
  segment_manager_->SetMainAreaStartBlock(LeToCpu(raw_super.main_blkaddr));
  segment_manager_->SetSegmentsCount(LeToCpu(raw_super.segment_count));
  segment_manager_->SetReservedSegmentsCount(LeToCpu(ckpt.rsvd_segment_count));
  segment_manager_->SetOPSegmentsCount(LeToCpu(ckpt.overprov_segment_count));
  segment_manager_->SetMainSegmentsCount(LeToCpu(raw_super.segment_count_main));
  segment_manager_->SetSSAreaStartBlock(LeToCpu(raw_super.ssa_blkaddr));

  if (zx_status_t ret = BuildSitInfo(); ret != ZX_OK) {
    return ret;
  }
  if (zx_status_t ret = BuildCurseg(); ret != ZX_OK) {
    return ret;
  }
  BuildSitEntries();
  return ZX_OK;
}

void FsckWorker::BuildSitAreaBitmap() {
  uint32_t sum_vblocks = 0;
  uint32_t free_segs = 0;
  uint32_t vblocks = 0;

  fsck_.sit_area_bitmap_size = segment_manager_->GetMainSegmentsCount() * kSitVBlockMapSize;
  fsck_.sit_area_bitmap.reset(new uint8_t[fsck_.sit_area_bitmap_size]());
  ZX_ASSERT(fsck_.sit_area_bitmap_size == fsck_.main_area_bitmap_size);
  uint8_t *ptr = fsck_.sit_area_bitmap.get();

  for (uint32_t segno = 0; segno < segment_manager_->GetMainSegmentsCount(); segno++) {
    SegmentEntry &segment_entry = GetSegmentEntry(segno);

    memcpy(ptr, segment_entry.cur_valid_map.get(), kSitVBlockMapSize);
    ptr += kSitVBlockMapSize;
    vblocks = 0;
    for (uint64_t j = 0; j < kSitVBlockMapSize; j++) {
      vblocks += std::bitset<8>(segment_entry.cur_valid_map[j]).count();
    }
    ZX_ASSERT(vblocks == segment_entry.valid_blocks);

    if (segment_entry.valid_blocks == 0x0) {
      if (superblock_info_.GetCheckpoint().cur_node_segno[0] == segno ||
          superblock_info_.GetCheckpoint().cur_data_segno[0] == segno ||
          superblock_info_.GetCheckpoint().cur_node_segno[1] == segno ||
          superblock_info_.GetCheckpoint().cur_data_segno[1] == segno ||
          superblock_info_.GetCheckpoint().cur_node_segno[2] == segno ||
          superblock_info_.GetCheckpoint().cur_data_segno[2] == segno) {
        continue;
      } else {
        ++free_segs;
      }
    } else {
      ZX_ASSERT(segment_entry.valid_blocks <= 512);
      sum_vblocks += segment_entry.valid_blocks;
    }
  }

#if 0  // TODO: implement debug level
  // TODO: DBG (1)
  printf("Blocks [0x%x : %d] Free Segs [0x%x : %d]\n\n", sum_vblocks, sum_vblocks, free_segs,
         free_segs);
#endif

  fsck_.result.sit_valid_blocks = sum_vblocks;
  fsck_.result.sit_free_segments = free_segs;
}

zx::status<RawNatEntry> FsckWorker::LookupNatInJournal(nid_t nid) {
  RawNatEntry raw_nat;
  CursegInfo *curseg = segment_manager_->CURSEG_I(CursegType::kCursegHotData);
  SummaryBlock *sum = curseg->sum_blk;

  for (int i = 0; i < NatsInCursum(sum); i++) {
    if (LeToCpu(NidInJournal(sum, i)) == nid) {
      RawNatEntry ret = NatInJournal(sum, i);
#if 0  // TODO: implement debug level
      // TODO: DBG (3)
      printf("==> Found nid [0x%x] in nat cache\n", nid);
#endif
      memcpy(&raw_nat, &ret, sizeof(RawNatEntry));
      return zx::ok(raw_nat);
    }
  }
  return zx::error(ZX_ERR_NOT_FOUND);
}

void FsckWorker::BuildNatAreaBitmap() {
  const Superblock &raw_sb = superblock_info_.GetRawSuperblock();
  nid_t nid, nr_nat_blks;

  block_t block_off;
  block_t block_addr;
  block_t seg_off;

  // Alloc & build nat entry bitmap
  nr_nat_blks = (LeToCpu(raw_sb.segment_count_nat) / 2) << superblock_info_.GetLogBlocksPerSeg();

  fsck_.nr_nat_entries = nr_nat_blks * kNatEntryPerBlock;
  fsck_.nat_area_bitmap_size = (fsck_.nr_nat_entries + 7) / 8;
  fsck_.nat_area_bitmap.reset(new uint8_t[fsck_.nat_area_bitmap_size]());
  ZX_ASSERT(fsck_.nat_area_bitmap.get() != nullptr);

  for (block_off = 0; block_off < nr_nat_blks; block_off++) {
    seg_off = block_off >> superblock_info_.GetLogBlocksPerSeg();
    block_addr = node_manager_->GetNatAddress() +
                 (seg_off << superblock_info_.GetLogBlocksPerSeg() << 1) +
                 (block_off & ((1 << superblock_info_.GetLogBlocksPerSeg()) - 1));

    if (TestValidBitmap(block_off, node_manager_->GetNatBitmap())) {
      block_addr += superblock_info_.GetBlocksPerSeg();
    }

    FsBlock fs_block;
    ZX_ASSERT(ReadBlock(fs_block, block_addr) == ZX_OK);
#ifdef __Fuchsia__
    NatBlock *nat_block = reinterpret_cast<NatBlock *>(fs_block.GetData().data());
#else   // __Fuchsia__
    NatBlock *nat_block = reinterpret_cast<NatBlock *>(fs_block.GetData());
#endif  // __Fuchsia__

    nid = block_off * kNatEntryPerBlock;
    for (uint32_t i = 0; i < kNatEntryPerBlock; i++) {
      NodeInfo node_info;
      node_info.nid = nid + i;

      if ((nid + i) == superblock_info_.GetNodeIno() ||
          (nid + i) == superblock_info_.GetMetaIno()) {
        ZX_ASSERT(nat_block->entries[i].block_addr != 0x0);
        continue;
      }

      if (auto result = LookupNatInJournal(nid + i); result.is_ok()) {
        RawNatEntry raw_nat = *result;
        NodeInfoFromRawNat(node_info, raw_nat);
        if (node_info.blk_addr != kNullAddr) {
          SetValidBitmap(nid + i, fsck_.nat_area_bitmap.get());
          ++fsck_.result.valid_nat_entry_count;
#if 0  // TODO: implement debug level
       // TODO: DBG (3)
          printf("nid[0x%x] in nat cache\n", nid + i);
#endif
        }
      } else {
        NodeInfoFromRawNat(node_info, nat_block->entries[i]);
        if (node_info.blk_addr != kNullAddr) {
          ZX_ASSERT(nid + i != 0x0);
#if 0  // TODO: implement debug level
       // TODO: DBG (3)
          printf("nid[0x%8x] in nat entry [0x%16x] [0x%8x]\n", nid + i, ni.blk_addr, ni.ino);
#endif
          SetValidBitmap(nid + i, fsck_.nat_area_bitmap.get());
          ++fsck_.result.valid_nat_entry_count;
        }
      }
    }
  }
#if 0  // TODO: implement debug level
  // TODO: DBG (1)
  printf("valid nat entries (block_addr != 0x0) [0x%8x : %u]\n", fsck->chk.valid_nat_entry_cnt,
         fsck->chk.valid_nat_entry_cnt);
#endif
}

zx_status_t FsckWorker::DoMount() {
  zx_status_t ret;
  superblock_info_.SetActiveLogs(kNrCursegType);

  if (ret = ValidateSuperblock(0); ret != ZX_OK) {
    if (ret = ValidateSuperblock(1); ret != ZX_OK) {
      return ret;
    }
  }

  PrintRawSuperblockInfo();
  InitSuperblockInfo();

  if (ret = GetValidCheckpoint(); ret != ZX_OK) {
    FX_LOGS(ERROR) << "Can't find valid checkpoint" << ret;
    return ret;
  }
  if (ret = SanityCheckCkpt(); ret != ZX_OK) {
    FX_LOGS(ERROR) << "Checkpoint is polluted" << ret;
    return ret;
  }

  PrintCheckpointInfo();
  superblock_info_.SetTotalValidNodeCount(
      LeToCpu(superblock_info_.GetCheckpoint().valid_node_count));
  superblock_info_.SetTotalValidInodeCount(
      LeToCpu(superblock_info_.GetCheckpoint().valid_inode_count));
  superblock_info_.SetUserBlockCount(
      LeToCpu(static_cast<block_t>(superblock_info_.GetCheckpoint().user_block_count)));
  superblock_info_.SetTotalValidBlockCount(
      LeToCpu(static_cast<block_t>(superblock_info_.GetCheckpoint().valid_block_count)));
  superblock_info_.SetLastValidBlockCount(superblock_info_.GetTotalValidBlockCount());
  superblock_info_.SetAllocValidBlockCount(0);

  if (ret = BuildSegmentManager(); ret != ZX_OK) {
    FX_LOGS(ERROR) << "build_segment_manager failed: " << ret;
    return ret;
  }
  if (ret = BuildNodeManager(); ret != ZX_OK) {
    FX_LOGS(ERROR) << "build_segment_manager failed: " << ret;
    return ret;
  }
  return ret;
}

void FsckWorker::DoUmount() {
  SitInfo &sit_i = segment_manager_->GetSitInfo();

  node_manager_.reset();
  for (uint32_t i = 0; i < segment_manager_->TotalSegs(); ++i) {
    sit_i.sentries[i].cur_valid_map.reset();
    sit_i.sentries[i].ckpt_valid_map.reset();
  }
  delete[] sit_i.sentries;
  sit_i.sit_bitmap.reset();

  for (uint32_t i = 0; i < kNrCursegType; ++i) {
    CursegInfo *curseg = segment_manager_->CURSEG_I(static_cast<CursegType>(i));
    delete curseg->raw_blk;
  }

  segment_manager_.reset();
}

zx_status_t FsckWorker::DoFsck() {
  uint32_t block_count;
  zx_status_t ret = ZX_OK;
  if (ret = Init(); ret != ZX_OK) {
    return ret;
  }

  CheckOrphanNode();
  FX_LOGS(INFO) << "checking orphan node.. done";

  // Travses all block recursively from root inode
  block_count = 1;
  ret = CheckNodeBlock(nullptr, superblock_info_.GetRootIno(), FileType::kFtDir,
                       NodeType::kTypeInode, block_count);
  FX_LOGS(INFO) << "checking node blocks.. done: " << ret;
  if (ret != ZX_OK) {
    return ret;
  }

  ret = Verify();
  FX_LOGS(INFO) << "verifying.. done: " << ret;
  return ret;
}

zx_status_t FsckWorker::Run() {
  zx_status_t ret = ZX_OK;
  if (ret = DoMount(); ret != ZX_OK) {
    return ret;
  }

  ret = DoFsck();
#if 0  // porting needed
  // ret = DoDump(superblock_info);
#endif
  DoUmount();
  FX_LOGS(INFO) << "Fsck.. done: " << ret;
  return ret;
}

zx_status_t Fsck(std::unique_ptr<Bcache> bc, std::unique_ptr<Bcache> *out) {
  zx_status_t ret;
  FsckWorker fsck(std::move(bc));
  ret = fsck.Run();
  if (out != nullptr) {
    *out = fsck.Destroy();
  }
  return ret;
}

}  // namespace f2fs
