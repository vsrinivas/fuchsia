// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <bitset>
#include <iostream>

#include "src/storage/f2fs/f2fs.h"

namespace f2fs::fsck {

using Block = FsBlock;

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

static inline uint64_t BlkoffFromMain(SbInfo *sbi, uint64_t blk_addr) {
  ZX_ASSERT(blk_addr >= GetSmInfo(sbi)->main_blkaddr);
  return blk_addr - GetSmInfo(sbi)->main_blkaddr;
}

static inline uint32_t OffsetInSeg(SbInfo *sbi, uint64_t blk_addr) {
  return (uint32_t)(BlkoffFromMain(sbi, blk_addr) % (1 << sbi->log_blocks_per_seg));
}

static inline uint32_t AddrsPerInode(Inode *i) {
#if 0  // porting needed
	      if (i->i_inline & kInlineXattr)
					            return kAddrPerInode - kInlineXattrAddrs;
#endif
  return kAddrsPerInode;
}

zx_status_t Fsck(Bcache *bc) {
  FsckWorker fsck(bc);
  return fsck.Run();
}

zx_status_t FsckWorker::ReadBlock(void *data, uint64_t bno) {
  return bc_->Readblk(static_cast<block_t>(bno), data);
}

void FsckWorker::AddIntoHardLinkList(uint32_t nid, uint32_t link_cnt) {
  FsckInfo *fsck = &fsck_;
  HardLinkNode *node = nullptr, *tmp = nullptr, *prev = nullptr;

  node = new HardLinkNode();
  ZX_ASSERT(node != nullptr);

  node->nid = nid;
  node->links = link_cnt;
  node->next = nullptr;

  if (fsck->hard_link_list_head == nullptr) {
    fsck->hard_link_list_head = node;
  } else {
    tmp = fsck->hard_link_list_head;

    // Find insertion position
    while (tmp && (nid < tmp->nid)) {
      ZX_ASSERT(tmp->nid != nid);
      prev = tmp;
      tmp = tmp->next;
    }

    if (tmp == fsck->hard_link_list_head) {
      node->next = tmp;
      fsck->hard_link_list_head = node;
    } else {
      prev->next = node;
      node->next = tmp;
    }
  }
  FX_LOGS(INFO) << "ino[0x" << std::hex << nid << "] has hard links [0x" << link_cnt << "]";
}

zx_status_t FsckWorker::FindAndDecHardLinkList(uint32_t nid) {
  FsckInfo *fsck = &fsck_;
  HardLinkNode *node = nullptr, *prev = nullptr;

  if (fsck->hard_link_list_head == nullptr) {
    ZX_ASSERT(0);
    return ZX_ERR_NOT_FOUND;
  }

  node = fsck->hard_link_list_head;

  while (node && (nid < node->nid)) {
    prev = node;
    node = node->next;
  }

  if (node == nullptr || (nid != node->nid)) {
    ZX_ASSERT(0);
    return ZX_ERR_NOT_FOUND;
  }

  // Decrease link count
  node->links = node->links - 1;

  // if link count becomes one, remove the node
  if (node->links == 1) {
    if (fsck->hard_link_list_head == node)
      fsck->hard_link_list_head = node->next;
    else
      prev->next = node->next;
    delete node;
  }

  return ZX_OK;
}

bool FsckWorker::IsValidSsaNodeBlk(uint32_t nid, uint32_t blk_addr) {
  Summary sum_entry;

  SegType ret = GetSumEntry(blk_addr, &sum_entry);
  ZX_ASSERT(static_cast<int>(ret) >= 0);

  if (ret == SegType::kSegTypeData || ret == SegType::kSegTypeCurData) {
    FX_LOGS(ERROR) << "Summary footer is not a node segment summary";
    ZX_ASSERT(0);
  } else if (ret == SegType::kSegTypeNode) {
    if (LeToCpu(sum_entry.nid) != nid) {
      FX_LOGS(ERROR) << "nid                       [0x" << std::hex << nid << "]";
      FX_LOGS(ERROR) << "target blk_addr           [0x" << std::hex << blk_addr << "]";
      FX_LOGS(ERROR) << "summary blk_addr          [0x" << std::hex
                     << GetSumBlock(&sbi_, GetSegNo(blk_addr)) << "]";
      FX_LOGS(ERROR) << "seg no / offset           [0x" << std::hex << GetSegNo(blk_addr) << "/0x"
                     << std::hex << OffsetInSeg(&sbi_, blk_addr) << "]";
      FX_LOGS(ERROR) << "summary_entry.nid         [0x" << std::hex << LeToCpu(sum_entry.nid)
                     << "]";
      FX_LOGS(ERROR) << "--> node block's nid      [0x" << std::hex << nid << "]";
      FX_LOGS(ERROR) << "Invalid node seg summary\n";
      ZX_ASSERT(0);
    }
  } else if (ret == SegType::kSegTypeCurNode) {
    // current node segment has no ssa
  } else {
    FX_LOGS(ERROR) << "Invalid return value of 'GetSumEntry'";
    ZX_ASSERT(0);
  }
  return true;
}

bool FsckWorker::IsValidSsaDataBlk(uint32_t blk_addr, uint32_t parent_nid, uint16_t idx_in_node,
                                   uint8_t version) {
  Summary sum_entry;

  SegType ret = GetSumEntry(blk_addr, &sum_entry);
  ZX_ASSERT(ret == SegType::kSegTypeData || ret == SegType::kSegTypeCurData);

  if (LeToCpu(sum_entry.nid) != parent_nid || sum_entry.version != version ||
      LeToCpu(sum_entry.ofs_in_node) != idx_in_node) {
    FX_LOGS(ERROR) << "summary_entry.nid         [0x" << std::hex << LeToCpu(sum_entry.nid) << "]";
    FX_LOGS(ERROR) << "summary_entry.version     [0x" << std::hex << sum_entry.version << "]";
    FX_LOGS(ERROR) << "summary_entry.ofs_in_node [0x" << std::hex << LeToCpu(sum_entry.ofs_in_node)
                   << "]";

    FX_LOGS(ERROR) << "parent nid                [0x" << std::hex << parent_nid << "]";
    FX_LOGS(ERROR) << "version from nat          [0x" << std::hex << version << "]";
    FX_LOGS(ERROR) << "idx in parent node        [0x" << std::hex << idx_in_node << "]";

    FX_LOGS(ERROR) << "Target data block addr    [0x" << std::hex << blk_addr << "]";
    FX_LOGS(ERROR) << "Invalid data seg summary\n";
    ZX_ASSERT(0);
  }
  return true;
}

zx_status_t FsckWorker::ChkNodeBlk(Inode *inode, uint32_t nid, FileType ftype, NodeType ntype,
                                   uint32_t *blk_cnt) {
  FsckInfo *fsck = &fsck_;
  NodeInfo ni;
  Node *node_blk = nullptr;
  zx_status_t ret = ZX_OK;
  SbInfo *sbi = &sbi_;

  IsValidNid(nid);

  if (ftype != FileType::kFtOrphan || TestValidBitmap(nid, fsck->nat_area_bitmap) != 0x0)
    ClearValidBitmap(nid, fsck->nat_area_bitmap);
  else {
    FX_LOGS(ERROR) << "nid duplicated [0x" << std::hex << nid << "]";
  }

  ret = GetNodeInfo(nid, &ni);
  ZX_ASSERT(ret == ZX_OK);

  // Is it reserved block?
  // if block addresss was kNewAddr
  // it means that block was already allocated, but not stored in disk
  if (ni.blk_addr == kNewAddr) {
    fsck->chk.valid_blk_cnt++;
    fsck->chk.valid_node_cnt++;
    if (ntype == NodeType::kTypeInode)
      fsck->chk.valid_inode_cnt++;
    return ZX_OK;
  }

  IsValidBlkAddr(ni.blk_addr);
  IsValidSsaNodeBlk(nid, ni.blk_addr);

  if (TestValidBitmap(BlkoffFromMain(sbi, ni.blk_addr), fsck->sit_area_bitmap) == 0x0) {
    FX_LOGS(INFO) << "SIT bitmap is 0x0. blk_addr[0x" << std::hex << ni.blk_addr << "]";
    ZX_ASSERT(0);
  }

  if (TestValidBitmap(BlkoffFromMain(sbi, ni.blk_addr), fsck->main_area_bitmap) == 0x0) {
    fsck->chk.valid_blk_cnt++;
    fsck->chk.valid_node_cnt++;
  }

  Block *blk = new Block;
  ZX_ASSERT(blk != nullptr);
  node_blk = reinterpret_cast<Node *>(blk->data);
  ret = ReadBlock(node_blk, ni.blk_addr);
  ZX_ASSERT(ret == ZX_OK);
  ZX_ASSERT_MSG(nid == LeToCpu(node_blk->footer.nid), "nid[0x%x] blk_addr[0x%x] footer.nid[0x%x]\n",
                nid, ni.blk_addr, LeToCpu(node_blk->footer.nid));

  if (ntype == NodeType::kTypeInode) {
    ret = ChkInodeBlk(nid, ftype, node_blk, blk_cnt, &ni);
  } else {
    // it's not inode
    ZX_ASSERT(node_blk->footer.nid != node_blk->footer.ino);

    if (TestValidBitmap(BlkoffFromMain(sbi, ni.blk_addr), fsck->main_area_bitmap) != 0) {
      FX_LOGS(INFO) << "Duplicated node block. ino[0x" << std::hex << nid << "][0x" << std::hex
                    << ni.blk_addr;
      ZX_ASSERT(0);
    }
    SetValidBitmap(BlkoffFromMain(sbi, ni.blk_addr), fsck->main_area_bitmap);

    switch (ntype) {
      case NodeType::kTypeDirectNode:
        ChkDnodeBlk(inode, nid, ftype, node_blk, blk_cnt, &ni);
        break;
      case NodeType::kTypeIndirectNode:
        ChkIdnodeBlk(inode, nid, ftype, node_blk, blk_cnt);
        break;
      case NodeType::kTypeDoubleIndirectNode:
        ChkDidnodeBlk(inode, nid, ftype, node_blk, blk_cnt);
        break;
      default:
        ZX_ASSERT(0);
    }
  }

  ZX_ASSERT(ret == ZX_OK);

  delete blk;
  return ZX_OK;
}

zx_status_t FsckWorker::ChkInodeBlk(uint32_t nid, FileType ftype, Node *node_blk, uint32_t *blk_cnt,
                                    NodeInfo *ni) {
  FsckInfo *fsck = &fsck_;
  uint32_t child_cnt = 0, child_files = 0;
  NodeType ntype;
  uint32_t i_links = LeToCpu(node_blk->i.i_links);
  uint64_t i_blocks = LeToCpu(node_blk->i.i_blocks);
  uint16_t idx = 0;

  ZX_ASSERT(node_blk->footer.nid == node_blk->footer.ino);
  ZX_ASSERT(LeToCpu(node_blk->footer.nid) == nid);

  if (TestValidBitmap(BlkoffFromMain(&sbi_, ni->blk_addr), fsck->main_area_bitmap) == 0x0)
    fsck->chk.valid_inode_cnt++;

  // Orphan node. i_links should be 0
  if (ftype == FileType::kFtOrphan) {
    ZX_ASSERT(i_links == 0);
  } else {
    ZX_ASSERT(i_links > 0);
  }

  if (ftype == FileType::kFtDir) {
    // not included '.' & '..'
    if (TestValidBitmap(BlkoffFromMain(&sbi_, ni->blk_addr), fsck->main_area_bitmap) != 0) {
      FX_LOGS(INFO) << "Duplicated inode blk. ino[0x" << std::hex << nid << "][0x" << std::hex
                    << ni->blk_addr;
      ZX_ASSERT(0);
    }
    SetValidBitmap(BlkoffFromMain(&sbi_, ni->blk_addr), fsck->main_area_bitmap);

  } else {
    if (TestValidBitmap(BlkoffFromMain(&sbi_, ni->blk_addr), fsck->main_area_bitmap) == 0x0) {
      SetValidBitmap(BlkoffFromMain(&sbi_, ni->blk_addr), fsck->main_area_bitmap);
      if (i_links > 1) {
        // First time. Create new hard link node
        AddIntoHardLinkList(nid, i_links);
        fsck->chk.multi_hard_link_files++;
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
      zx_status_t status = FindAndDecHardLinkList(nid);
      ZX_ASSERT(status == ZX_OK);

      // No need to go deep into the node
      return ZX_OK;
    }
  }
#if 0  // porting needed
  fsck_chk_xattr_blk(sbi, nid, LeToCpu(node_blk->i.i_xattr_nid), blk_cnt);
#endif

  do {
    if (ftype == FileType::kFtChrdev || ftype == FileType::kFtBlkdev ||
        ftype == FileType::kFtFifo || ftype == FileType::kFtSock)
      break;
#if 0  // porting needed
  if ((node_blk->i.i_inline & F2FS_INLINE_DATA)) {
    FX_LOGS(INFO) << "ino[0x" << std::hex << nid << "] has inline data";
    break;
  }
#endif

    // check data blocks in inode
    for (idx = 0; idx < AddrsPerInode(&node_blk->i); idx++) {
      if (LeToCpu(node_blk->i.i_addr[idx]) != 0) {
        *blk_cnt = *blk_cnt + 1;
        zx_status_t ret =
            ChkDataBlk(&node_blk->i, LeToCpu(node_blk->i.i_addr[idx]), &child_cnt, &child_files,
                       (i_blocks == *blk_cnt), ftype, nid, idx, ni->version);
        ZX_ASSERT(ret == ZX_OK);
      }
    }

    // check node blocks in inode
    for (idx = 0; idx < 5; idx++) {
      if (idx == 0 || idx == 1)
        ntype = NodeType::kTypeDirectNode;
      else if (idx == 2 || idx == 3)
        ntype = NodeType::kTypeIndirectNode;
      else if (idx == 4)
        ntype = NodeType::kTypeDoubleIndirectNode;
      else
        ZX_ASSERT(0);

      if (LeToCpu(node_blk->i.i_nid[idx]) != 0) {
        *blk_cnt = *blk_cnt + 1;
        zx_status_t ret =
            ChkNodeBlk(&node_blk->i, LeToCpu(node_blk->i.i_nid[idx]), ftype, ntype, blk_cnt);
        ZX_ASSERT(ret == ZX_OK);
      }
    }
  } while (0);
#ifdef F2FS_BU_DEBUG
  if (ftype == FileType::kFtDir)  // TODO: DBG(1)
    printf("Directory Inode: ino: %x name: %s depth: %d child files: %d\n\n",
           LeToCpu(node_blk->footer.ino), node_blk->i.i_name, LeToCpu(node_blk->i.i_current_depth),
           child_files);
  if (ftype == FileType::kFtOrphan)  // TODO: DBG (1)
    printf("Orphan Inode: ino: %x name: %s i_blocks: %u\n\n", LeToCpu(node_blk->footer.ino),
           node_blk->i.i_name, (uint32_t)i_blocks);
#endif
  if ((ftype == FileType::kFtDir && i_links != child_cnt) || (i_blocks != *blk_cnt)) {
    PrintNodeInfo(node_blk);
#ifdef F2FS_BU_DEBUG
    // TODO: DBG (1)
    printf("blk   cnt [0x%x]\n", *blk_cnt);
    // TODO: DBG (1)
    printf("child cnt [0x%x]\n", child_cnt);
#endif
  }

  ZX_ASSERT(i_blocks == *blk_cnt);
  if (ftype == FileType::kFtDir)
    ZX_ASSERT(i_links == child_cnt);
  return ZX_OK;
}

void FsckWorker::ChkDnodeBlk(Inode *inode, uint32_t nid, FileType ftype, Node *node_blk,
                             uint32_t *blk_cnt, NodeInfo *ni) {
  uint32_t child_cnt = 0, child_files = 0;
  for (uint16_t idx = 0; idx < kAddrsPerBlock; idx++) {
    if (LeToCpu(node_blk->dn.addr[idx]) == 0x0)
      continue;
    *blk_cnt = *blk_cnt + 1;
    ChkDataBlk(inode, LeToCpu(node_blk->dn.addr[idx]), &child_cnt, &child_files,
               LeToCpu(inode->i_blocks) == *blk_cnt, ftype, nid, idx, ni->version);
  }
}

void FsckWorker::ChkIdnodeBlk(Inode *inode, uint32_t nid, FileType ftype, Node *node_blk,
                              uint32_t *blk_cnt) {
  for (uint32_t i = 0; i < kNidsPerBlock; i++) {
    if (LeToCpu(node_blk->in.nid[i]) == 0x0)
      continue;
    *blk_cnt = *blk_cnt + 1;
    ChkNodeBlk(inode, LeToCpu(node_blk->in.nid[i]), ftype, NodeType::kTypeDirectNode, blk_cnt);
  }
}

void FsckWorker::ChkDidnodeBlk(Inode *inode, uint32_t nid, FileType ftype, Node *node_blk,
                               uint32_t *blk_cnt) {
  int i = 0;

  for (i = 0; i < kNidsPerBlock; i++) {
    if (LeToCpu(node_blk->in.nid[i]) == 0x0)
      continue;
    *blk_cnt = *blk_cnt + 1;
    ChkNodeBlk(inode, LeToCpu(node_blk->in.nid[i]), ftype, NodeType::kTypeIndirectNode, blk_cnt);
  }
}

void FsckWorker::PrintDentry(uint32_t depth, std::string_view &name, DentryBlock *de_blk, int idx,
                             int last_blk) {
  int last_de = 0;
  int next_idx = 0;
  int name_len;
  uint32_t i;
  int bit_offset;

#if 0  // porting needed
  if (config.dbg_lv != -1)
    return;
#endif

  name_len = LeToCpu(de_blk->dentry[idx].name_len);
  next_idx = idx + (name_len + kDentrySlotLen - 1) / kDentrySlotLen;

  bit_offset = FindNextBit(de_blk->dentry_bitmap, kNrDentryInBlock, next_idx);
  if (bit_offset >= kNrDentryInBlock && last_blk)
    last_de = 1;

  if (tree_mark_.size() <= depth) {
    tree_mark_.resize(tree_mark_.size() * 2, 0);
  }
  if (last_de)
    tree_mark_[depth] = '`';
  else
    tree_mark_[depth] = '|';

  if (tree_mark_[depth - 1] == '`')
    tree_mark_[depth - 1] = ' ';

  for (i = 1; i < depth; i++)
    std::cout << tree_mark_[i] << "   ";
  std::cout << (last_de ? "`" : "|") << "-- " << name << std::endl;
}

void FsckWorker::ChkDentryBlk(Inode *inode, uint32_t blk_addr, uint32_t *child_cnt,
                              uint32_t *child_files, int last_blk) {
  FsckInfo *fsck = &fsck_;
  int i;
  int ret = 0;
  int dentries = 0;
  uint32_t hash_code;
  uint32_t blk_cnt;
  FileType ftype;
  DentryBlock *de_blk;

  Block *blk = new Block;
  ZX_ASSERT(blk != nullptr);
  de_blk = reinterpret_cast<DentryBlock *>(blk->data);

  ret = ReadBlock(de_blk, blk_addr);
  ZX_ASSERT(ret >= 0);

  fsck->dentry_depth++;

  for (i = 0; i < kNrDentryInBlock;) {
    if (TestBit(i, de_blk->dentry_bitmap) == 0x0) {
      i++;
      continue;
    }

    std::string_view name(reinterpret_cast<char *>(de_blk->filename[i]),
                          LeToCpu(de_blk->dentry[i].name_len));

    hash_code = DentryHash(name.data(), static_cast<int>(name.length()));

    ftype = static_cast<FileType>(de_blk->dentry[i].file_type);

    // Becareful. 'dentry.file_type' is not imode
    if (ftype == FileType::kFtDir) {
      *child_cnt = *child_cnt + 1;
      if (name.compare("..") == 0 || name.compare(".") == 0) {
        i++;
        continue;
      }
    }

    // TODO: Should we check '.' and '..' entries?
    ZX_ASSERT(LeToCpu(de_blk->dentry[i].hash_code) == hash_code);
#ifdef F2FS_BU_DEBUG
    // TODO: DBG (2)
    printf("[%3u] - no[0x%x] name[%s] len[0x%x] ino[0x%x] type[0x%x]\n", fsck->dentry_depth, i,
           name.data(), LeToCpu(de_blk->dentry[i].name_len), LeToCpu(de_blk->dentry[i].ino),
           de_blk->dentry[i].file_type);
#endif
    PrintDentry(fsck->dentry_depth, name, de_blk, i, last_blk);

    blk_cnt = 1;
    ret =
        ChkNodeBlk(nullptr, LeToCpu(de_blk->dentry[i].ino), ftype, NodeType::kTypeInode, &blk_cnt);

    ZX_ASSERT(ret >= 0);

    i += (name.length() + kDentrySlotLen - 1) / kDentrySlotLen;
    dentries++;
    *child_files = *child_files + 1;
  }
#ifdef F2FS_BU_DEBUG
  // TODO: DBG (1)
  printf("[%3d] Dentry Block [0x%x] Done : dentries:%d in %d slots (len:%d)\n\n",
         fsck->dentry_depth, blk_addr, dentries, kNrDentryInBlock, kMaxNameLen);
#endif
  fsck->dentry_depth--;

  delete blk;
}

zx_status_t FsckWorker::ChkDataBlk(Inode *inode, uint32_t blk_addr, uint32_t *child_cnt,
                                   uint32_t *child_files, int last_blk, FileType ftype,
                                   uint32_t parent_nid, uint16_t idx_in_node, uint8_t ver) {
  FsckInfo *fsck = &fsck_;

  // Is it reserved block?
  if (blk_addr == kNewAddr) {
    fsck->chk.valid_blk_cnt++;
    return ZX_OK;
  }

  IsValidBlkAddr(blk_addr);

  IsValidSsaDataBlk(blk_addr, parent_nid, idx_in_node, ver);

  if (TestValidBitmap(BlkoffFromMain(&sbi_, blk_addr), fsck->sit_area_bitmap) == 0x0) {
    ZX_ASSERT_MSG(0, "SIT bitmap is 0x0. blk_addr[0x%x]\n", blk_addr);
  }

  if (TestValidBitmap(BlkoffFromMain(&sbi_, blk_addr), fsck->main_area_bitmap) != 0) {
    ZX_ASSERT_MSG(0, "Duplicated data block. pnid[0x%x] idx[0x%x] blk_addr[0x%x]\n", parent_nid,
                  idx_in_node, blk_addr);
  }
  SetValidBitmap(BlkoffFromMain(&sbi_, blk_addr), fsck->main_area_bitmap);

  fsck->chk.valid_blk_cnt++;

  if (ftype == FileType::kFtDir) {
    ChkDentryBlk(inode, blk_addr, child_cnt, child_files, last_blk);
  }

  return ZX_OK;
}

void FsckWorker::ChkOrphanNode() {
  uint32_t blk_cnt = 0;
  block_t start_blk, orphan_blkaddr, i, j;
  OrphanBlock *orphan_blk;

  if (!IsSetCkptFlags(GetCheckpoint(&sbi_), kCpOrphanPresentFlag))
    return;

  start_blk = StartCpAddr(&sbi_) + 1;
  orphan_blkaddr = StartSumAddr(&sbi_) - 1;

  orphan_blk = new OrphanBlock();

  for (i = 0; i < orphan_blkaddr; i++) {
    ReadBlock(orphan_blk, start_blk + i);

    for (j = 0; j < LeToCpu(orphan_blk->entry_count); j++) {
      nid_t ino = LeToCpu(orphan_blk->ino[j]);
#ifdef F2FS_BU_DEBUG
      // TODO: DBG (1)
      printf("[%3d] ino [0x%x]\n", i, ino);
#endif
      blk_cnt = 1;
      zx_status_t ret =
          ChkNodeBlk(nullptr, ino, FileType::kFtOrphan, NodeType::kTypeInode, &blk_cnt);
      ZX_ASSERT(ret == ZX_OK);
    }
    memset(orphan_blk, 0, kBlockSize);
  }
  delete orphan_blk;
}

#if 0  // porting needed
int FsckWorker::FsckChkXattrBlk(uint32_t ino, uint32_t x_nid, uint32_t *blk_cnt) {
  FsckInfo *fsck = &fsck_;
  NodeInfo ni;

  if (x_nid == 0x0)
    return 0;

  if (TestValidBitmap(x_nid, fsck->nat_area_bitmap) != 0x0) {
    ClearValidBitmap(x_nid, fsck->nat_area_bitmap);
  } else {
    ZX_ASSERT_MSG(0, "xattr_nid duplicated [0x%x]\n", x_nid);
  }

  *blk_cnt = *blk_cnt + 1;
  fsck->chk.valid_blk_cnt++;
  fsck->chk.valid_node_cnt++;

  ZX_ASSERT(GetNodeInfo(x_nid, &ni) >= 0);

  if (TestValidBitmap(BlkoffFromMain(sbi, ni.blk_addr), fsck->main_area_bitmap) != 0) {
    ZX_ASSERT_MSG(0,
                  "Duplicated node block for x_attr. "
                  "x_nid[0x%x] block addr[0x%x]\n",
                  x_nid, ni.blk_addr);
  }
  SetValidBitmap(BlkoffFromMain(sbi, ni.blk_addr), fsck->main_area_bitmap);
#ifdef F2FS_BU_DEBUG
  // TODO: DBG (2)
  printf("ino[0x%x] x_nid[0x%x]\n", ino, x_nid);
#endif
  return 0;
}
#endif

zx_status_t FsckWorker::Init() {
  FsckInfo *fsck = &fsck_;
  SmInfo *sm_i = GetSmInfo(&sbi_);

  fsck->nr_main_blks = sm_i->main_segments << sbi_.log_blocks_per_seg;
  fsck->main_area_bitmap_sz = (fsck->nr_main_blks + 7) / 8;
  fsck->main_area_bitmap = new char[fsck->main_area_bitmap_sz];
  ZX_ASSERT(fsck->main_area_bitmap != nullptr);
  memset(fsck->main_area_bitmap, 0, fsck->main_area_bitmap_sz);

  BuildNatAreaBitmap();
  BuildSitAreaBitmap();

  return ZX_OK;
}

zx_status_t FsckWorker::Verify() {
  uint32_t i = 0;
  zx_status_t ret = ZX_OK;
  uint32_t nr_unref_nid = 0;
  FsckInfo *fsck = &fsck_;
  HardLinkNode *node = nullptr;

  printf("\n");

  for (i = 0; i < fsck->nr_nat_entries; i++) {
    if (TestValidBitmap(i, fsck->nat_area_bitmap) != 0) {
      printf("NID[0x%x] is unreachable\n", i);
      nr_unref_nid++;
    }
  }

  if (fsck->hard_link_list_head != nullptr) {
    node = fsck->hard_link_list_head;
    while (node) {
      printf("NID[0x%x] has [0x%x] more unreachable links\n", node->nid, node->links);
      node = node->next;
    }
  }

  printf("[FSCK] Unreachable nat entries                       ");
  if (nr_unref_nid == 0x0) {
    printf(" [Ok..] [0x%x]\n", nr_unref_nid);
  } else {
    printf(" [Fail] [0x%x]\n", nr_unref_nid);
    ret = ZX_ERR_BAD_STATE;
  }

  printf("[FSCK] SIT valid block bitmap checking                ");
  if (memcmp(fsck->sit_area_bitmap, fsck->main_area_bitmap, fsck->sit_area_bitmap_sz) == 0x0) {
    printf("[Ok..]\n");
  } else {
    printf("[Fail]\n");
    ret = ZX_ERR_BAD_STATE;
  }

  printf("[FSCK] Hard link checking for regular file           ");
  if (fsck->hard_link_list_head == nullptr) {
    printf(" [Ok..] [0x%x]\n", fsck->chk.multi_hard_link_files);
  } else {
    printf(" [Fail] [0x%x]\n", fsck->chk.multi_hard_link_files);
    ret = ZX_ERR_BAD_STATE;
  }

  printf("[FSCK] valid_block_count matching with CP            ");
  if (sbi_.total_valid_block_count == fsck->chk.valid_blk_cnt) {
    printf(" [Ok..] [0x%x]\n", (uint32_t)fsck->chk.valid_blk_cnt);
  } else {
    printf(" [Fail] [0x%x]\n", (uint32_t)fsck->chk.valid_blk_cnt);
    ret = ZX_ERR_BAD_STATE;
  }

  printf("[FSCK] valid_node_count matcing with CP (de lookup)  ");
  if (sbi_.total_valid_node_count == fsck->chk.valid_node_cnt) {
    printf(" [Ok..] [0x%x]\n", fsck->chk.valid_node_cnt);
  } else {
    printf(" [Fail] [0x%x]\n", fsck->chk.valid_node_cnt);
    ret = ZX_ERR_BAD_STATE;
  }

  printf("[FSCK] valid_node_count matcing with CP (nat lookup) ");
  if (sbi_.total_valid_node_count == fsck->chk.valid_nat_entry_cnt) {
    printf(" [Ok..] [0x%x]\n", fsck->chk.valid_nat_entry_cnt);
  } else {
    printf(" [Fail] [0x%x]\n", fsck->chk.valid_nat_entry_cnt);
    ret = ZX_ERR_BAD_STATE;
  }

  printf("[FSCK] valid_inode_count matched with CP             ");
  if (sbi_.total_valid_inode_count == fsck->chk.valid_inode_cnt) {
    printf(" [Ok..] [0x%x]\n", fsck->chk.valid_inode_cnt);
  } else {
    printf(" [Fail] [0x%x]\n", fsck->chk.valid_inode_cnt);
    ret = ZX_ERR_BAD_STATE;
  }

  return ret;
}

void FsckWorker::Free() {
  FsckInfo *fsck = &fsck_;
  if (fsck->main_area_bitmap != nullptr)
    delete[] fsck->main_area_bitmap;

  if (fsck->nat_area_bitmap != nullptr)
    delete[] fsck->nat_area_bitmap;

  if (fsck->sit_area_bitmap != nullptr)
    delete[] fsck->sit_area_bitmap;
}

void FsckWorker::PrintInodeInfo(Inode *inode) {
  uint32_t i = 0;
  int namelen = LeToCpu(inode->i_namelen);

  DisplayMember(sizeof(uint32_t), inode->i_mode, "i_mode");
  DisplayMember(sizeof(uint32_t), inode->i_uid, "i_uid");
  DisplayMember(sizeof(uint32_t), inode->i_gid, "i_gid");
  DisplayMember(sizeof(uint32_t), inode->i_links, "i_links");
  DisplayMember(sizeof(uint64_t), inode->i_size, "i_size");
  DisplayMember(sizeof(uint64_t), inode->i_blocks, "i_blocks");

  DisplayMember(sizeof(uint64_t), inode->i_atime, "i_atime");
  DisplayMember(sizeof(uint32_t), inode->i_atime_nsec, "i_atime_nsec");
  DisplayMember(sizeof(uint64_t), inode->i_ctime, "i_ctime");
  DisplayMember(sizeof(uint32_t), inode->i_ctime_nsec, "i_ctime_nsec");
  DisplayMember(sizeof(uint64_t), inode->i_mtime, "i_mtime");
  DisplayMember(sizeof(uint32_t), inode->i_mtime_nsec, "i_mtime_nsec");

  DisplayMember(sizeof(uint32_t), inode->i_generation, "i_generation");
  DisplayMember(sizeof(uint32_t), inode->i_current_depth, "i_current_depth");
  DisplayMember(sizeof(uint32_t), inode->i_xattr_nid, "i_xattr_nid");
  DisplayMember(sizeof(uint32_t), inode->i_flags, "i_flags");
  DisplayMember(sizeof(uint32_t), inode->i_pino, "i_pino");

  if (namelen) {
    DisplayMember(sizeof(uint32_t), inode->i_namelen, "i_namelen");
    inode->i_name[namelen] = '\0';
    DisplayMember(sizeof(char), inode->i_name, "i_name");
  }

  printf("i_ext: fofs:%x blkaddr:%x len:%x\n", inode->i_ext.fofs, inode->i_ext.blk_addr,
         inode->i_ext.len);

  DisplayMember(sizeof(uint32_t), inode->i_addr[0], "i_addr[0]");  // Pointers to data blocks
  DisplayMember(sizeof(uint32_t), inode->i_addr[1], "i_addr[1]");  // Pointers to data blocks
  DisplayMember(sizeof(uint32_t), inode->i_addr[2], "i_addr[2]");  // Pointers to data blocks
  DisplayMember(sizeof(uint32_t), inode->i_addr[3], "i_addr[3]");  // Pointers to data blocks

  for (i = 4; i < AddrsPerInode(inode); i++) {
    if (inode->i_addr[i] != 0x0) {
      printf("i_addr[0x%x] points data block\r\t\t\t\t[0x%4x]\n", i, inode->i_addr[i]);
      break;
    }
  }

  DisplayMember(sizeof(uint32_t), inode->i_nid[0], "i_nid[0]");  // direct
  DisplayMember(sizeof(uint32_t), inode->i_nid[1], "i_nid[1]");  // direct
  DisplayMember(sizeof(uint32_t), inode->i_nid[2], "i_nid[2]");  // indirect
  DisplayMember(sizeof(uint32_t), inode->i_nid[3], "i_nid[3]");  // indirect
  DisplayMember(sizeof(uint32_t), inode->i_nid[4], "i_nid[4]");  // double indirect

  printf("\n");
}

void FsckWorker::PrintNodeInfo(Node *node_block) {
  nid_t ino = LeToCpu(node_block->footer.ino);
  nid_t nid = LeToCpu(node_block->footer.nid);
  if (ino == nid) {
    FX_LOGS(INFO) << "Node ID [0x" << std::hex << nid << ":" << nid << "] is inode";
    PrintInodeInfo(&node_block->i);
  } else {
    int i;
    uint32_t *dump_blk = (uint32_t *)node_block;
    FX_LOGS(INFO) << "Node ID [0x" << std::hex << nid << ":" << nid
                  << "] is direct node or indirect node";
    for (i = 0; i <= 10; i++)  // MSG (0)
      printf("[%d]\t\t\t[0x%8x : %d]\n", i, dump_blk[i], dump_blk[i]);
  }
}

void FsckWorker::PrintRawSbInfo() {
  const SuperBlock *sb = RawSuper(&sbi_);
#if 0  // porting needed
  if (!config.dbg_lv)
    return;
#endif

  printf("\n");
  printf("+--------------------------------------------------------+\n");
  printf("| Super block                                            |\n");
  printf("+--------------------------------------------------------+\n");

  DisplayMember(sizeof(uint32_t), sb->magic, "magic");
  DisplayMember(sizeof(uint32_t), sb->major_ver, "major_ver");
  DisplayMember(sizeof(uint32_t), sb->minor_ver, "minor_ver");
  DisplayMember(sizeof(uint32_t), sb->log_sectorsize, "log_sectorsize");
  DisplayMember(sizeof(uint32_t), sb->log_sectors_per_block, "log_sectors_per_block");

  DisplayMember(sizeof(uint32_t), sb->log_blocksize, "log_blocksize");
  DisplayMember(sizeof(uint32_t), sb->log_blocks_per_seg, "log_blocks_per_seg");
  DisplayMember(sizeof(uint32_t), sb->segs_per_sec, "segs_per_sec");
  DisplayMember(sizeof(uint32_t), sb->secs_per_zone, "secs_per_zone");
  DisplayMember(sizeof(uint32_t), sb->checksum_offset, "checksum_offset");
  DisplayMember(sizeof(uint64_t), sb->block_count, "block_count");

  DisplayMember(sizeof(uint32_t), sb->section_count, "section_count");
  DisplayMember(sizeof(uint32_t), sb->segment_count, "segment_count");
  DisplayMember(sizeof(uint32_t), sb->segment_count_ckpt, "segment_count_ckpt");
  DisplayMember(sizeof(uint32_t), sb->segment_count_sit, "segment_count_sit");
  DisplayMember(sizeof(uint32_t), sb->segment_count_nat, "segment_count_nat");

  DisplayMember(sizeof(uint32_t), sb->segment_count_ssa, "segment_count_ssa");
  DisplayMember(sizeof(uint32_t), sb->segment_count_main, "segment_count_main");
  DisplayMember(sizeof(uint32_t), sb->segment0_blkaddr, "segment0_blkaddr");

  DisplayMember(sizeof(uint32_t), sb->cp_blkaddr, "cp_blkaddr");
  DisplayMember(sizeof(uint32_t), sb->sit_blkaddr, "sit_blkaddr");
  DisplayMember(sizeof(uint32_t), sb->nat_blkaddr, "nat_blkaddr");
  DisplayMember(sizeof(uint32_t), sb->ssa_blkaddr, "ssa_blkaddr");
  DisplayMember(sizeof(uint32_t), sb->main_blkaddr, "main_blkaddr");

  DisplayMember(sizeof(uint32_t), sb->root_ino, "root_ino");
  DisplayMember(sizeof(uint32_t), sb->node_ino, "node_ino");
  DisplayMember(sizeof(uint32_t), sb->meta_ino, "meta_ino");
  printf("\n");
}

void FsckWorker::PrintCkptInfo() {
  Checkpoint *cp = GetCheckpoint(&sbi_);
  uint32_t alloc_type;
#if 0  // porting needed
  if (!config.dbg_lv)
    return;
#endif

  printf("\n");
  printf("+--------------------------------------------------------+\n");
  printf("| Checkpoint                                             |\n");
  printf("+--------------------------------------------------------+\n");

  DisplayMember(sizeof(uint64_t), cp->checkpoint_ver, "checkpoint_ver");
  DisplayMember(sizeof(uint64_t), cp->user_block_count, "user_block_count");
  DisplayMember(sizeof(uint64_t), cp->valid_block_count, "valid_block_count");
  DisplayMember(sizeof(uint32_t), cp->rsvd_segment_count, "rsvd_segment_count");
  DisplayMember(sizeof(uint32_t), cp->overprov_segment_count, "overprov_segment_count");
  DisplayMember(sizeof(uint32_t), cp->free_segment_count, "free_segment_count");

  alloc_type = cp->alloc_type[static_cast<int>(CursegType::kCursegHotNode)];
  DisplayMember(sizeof(uint32_t), alloc_type, "alloc_type[CursegType::kCursegHotNode]");
  alloc_type = cp->alloc_type[static_cast<int>(CursegType::kCursegWarmNode)];
  DisplayMember(sizeof(uint32_t), alloc_type, "alloc_type[CursegType::kCursegWarmNode]");
  alloc_type = cp->alloc_type[static_cast<int>(CursegType::kCursegColdNode)];
  DisplayMember(sizeof(uint32_t), alloc_type, "alloc_type[CursegType::kCursegColdNode]");
  alloc_type = cp->alloc_type[static_cast<int>(CursegType::kCursegHotNode)];
  DisplayMember(sizeof(uint32_t), cp->cur_node_segno[0], "cur_node_segno[0]");
  DisplayMember(sizeof(uint32_t), cp->cur_node_segno[1], "cur_node_segno[1]");
  DisplayMember(sizeof(uint32_t), cp->cur_node_segno[2], "cur_node_segno[2]");

  DisplayMember(sizeof(uint32_t), cp->cur_node_blkoff[0], "cur_node_blkoff[0]");
  DisplayMember(sizeof(uint32_t), cp->cur_node_blkoff[1], "cur_node_blkoff[1]");
  DisplayMember(sizeof(uint32_t), cp->cur_node_blkoff[2], "cur_node_blkoff[2]");

  alloc_type = cp->alloc_type[static_cast<int>(CursegType::kCursegHotData)];
  DisplayMember(sizeof(uint32_t), alloc_type, "alloc_type[CursegType::kCursegHotData]");
  alloc_type = cp->alloc_type[static_cast<int>(CursegType::kCursegWarmData)];
  DisplayMember(sizeof(uint32_t), alloc_type, "alloc_type[CursegType::kCursegWarmData]");
  alloc_type = cp->alloc_type[static_cast<int>(CursegType::kCursegColdData)];
  DisplayMember(sizeof(uint32_t), alloc_type, "alloc_type[CursegType::kCursegColdData]");
  DisplayMember(sizeof(uint32_t), cp->cur_data_segno[0], "cur_data_segno[0]");
  DisplayMember(sizeof(uint32_t), cp->cur_data_segno[1], "cur_data_segno[1]");
  DisplayMember(sizeof(uint32_t), cp->cur_data_segno[2], "cur_data_segno[2]");

  DisplayMember(sizeof(uint32_t), cp->cur_data_blkoff[0], "cur_data_blkoff[0]");
  DisplayMember(sizeof(uint32_t), cp->cur_data_blkoff[1], "cur_data_blkoff[1]");
  DisplayMember(sizeof(uint32_t), cp->cur_data_blkoff[2], "cur_data_blkoff[2]");

  DisplayMember(sizeof(uint32_t), cp->ckpt_flags, "ckpt_flags");
  DisplayMember(sizeof(uint32_t), cp->cp_pack_total_block_count, "cp_pack_total_block_count");
  DisplayMember(sizeof(uint32_t), cp->cp_pack_start_sum, "cp_pack_start_sum");
  DisplayMember(sizeof(uint32_t), cp->valid_node_count, "valid_node_count");
  DisplayMember(sizeof(uint32_t), cp->valid_inode_count, "valid_inode_count");
  DisplayMember(sizeof(uint32_t), cp->next_free_nid, "next_free_nid");
  DisplayMember(sizeof(uint32_t), cp->sit_ver_bitmap_bytesize, "sit_ver_bitmap_bytesize");
  DisplayMember(sizeof(uint32_t), cp->nat_ver_bitmap_bytesize, "nat_ver_bitmap_bytesize");
  DisplayMember(sizeof(uint32_t), cp->checksum_offset, "checksum_offset");
  DisplayMember(sizeof(uint64_t), cp->elapsed_time, "elapsed_time");

  printf("\n\n");
}

zx_status_t FsckWorker::SanityCheckRawSuper(const SuperBlock *raw_super) {
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
  if (kLogSectorSize != LeToCpu(raw_super->log_sectorsize)) {
    return ZX_ERR_BAD_STATE;
  }
  if (kLogSectorsPerBlock != LeToCpu(raw_super->log_sectors_per_block)) {
    return ZX_ERR_BAD_STATE;
  }
  return ZX_OK;
}

zx_status_t FsckWorker::ValidateSuperblock(block_t block) {
  SuperBlock *sb = new SuperBlock();
  zx_status_t ret = ZX_OK;
  if (ret = LoadSuperblock(bc_, sb); ret != ZX_OK)
    return ret;

  if (ret = SanityCheckRawSuper(sb); ret == ZX_OK) {
    sbi_.raw_super = sb;
    return ret;
  }
  FX_LOGS(WARNING) << "Can't find a valid F2FS filesystem in" << block << "superblock";
  delete sb;
  return ret;
}

void FsckWorker::InitSbInfo() {
  const SuperBlock *raw_super = sbi_.raw_super;

  sbi_.log_sectors_per_block = LeToCpu(raw_super->log_sectors_per_block);
  sbi_.log_blocksize = LeToCpu(raw_super->log_blocksize);
  sbi_.blocksize = 1 << sbi_.log_blocksize;
  sbi_.log_blocks_per_seg = LeToCpu(raw_super->log_blocks_per_seg);
  sbi_.blocks_per_seg = 1 << sbi_.log_blocks_per_seg;
  sbi_.segs_per_sec = LeToCpu(raw_super->segs_per_sec);
  sbi_.secs_per_zone = LeToCpu(raw_super->secs_per_zone);
  sbi_.total_sections = LeToCpu(raw_super->section_count);
  sbi_.total_node_count =
      (LeToCpu(raw_super->segment_count_nat) / 2) * sbi_.blocks_per_seg * kNatEntryPerBlock;
  sbi_.root_ino_num = LeToCpu(raw_super->root_ino);
  sbi_.node_ino_num = LeToCpu(raw_super->node_ino);
  sbi_.meta_ino_num = LeToCpu(raw_super->meta_ino);
#if 0  // porting needed
  sbi->cur_victim_sec = kNullSegNo;
#endif
}

void *FsckWorker::ValidateCheckpoint(block_t cp_addr, uint64_t *version) {
  void *cp_page_1, *cp_page_2;
  Checkpoint *cp_block;
  uint64_t blk_size = sbi_.blocksize;
  uint64_t cur_version = 0, pre_version = 0;
  uint32_t crc = 0;
  size_t crc_offset;

  // Read the 1st cp block in this CP pack
  cp_page_1 = reinterpret_cast<Block *>(new Block);
  if (ReadBlock(cp_page_1, cp_addr) < 0)
    return nullptr;

  cp_block = (Checkpoint *)cp_page_1;
  crc_offset = LeToCpu(cp_block->checksum_offset);
  if (crc_offset >= blk_size) {
    delete reinterpret_cast<Block *>(cp_page_1);
    return nullptr;
  }

  crc = *(unsigned int *)((unsigned char *)cp_block + crc_offset);
  if (!F2fsCrcValid(crc, cp_block, static_cast<uint32_t>(crc_offset))) {
    delete reinterpret_cast<Block *>(cp_page_1);
    return nullptr;
  }

  pre_version = LeToCpu(cp_block->checkpoint_ver);

  // Read the 2nd cp block in this CP pack
  cp_page_2 = reinterpret_cast<Block *>(new Block);
  cp_addr += LeToCpu(cp_block->cp_pack_total_block_count) - 1;
  if (ReadBlock(cp_page_2, cp_addr) < 0) {
    delete reinterpret_cast<Block *>(cp_page_1);
    delete reinterpret_cast<Block *>(cp_page_2);
    return nullptr;
  }

  cp_block = (Checkpoint *)cp_page_2;
  crc_offset = LeToCpu(cp_block->checksum_offset);
  if (crc_offset >= blk_size) {
    delete reinterpret_cast<Block *>(cp_page_1);
    delete reinterpret_cast<Block *>(cp_page_2);
    return nullptr;
  }

  crc = *(unsigned int *)((unsigned char *)cp_block + crc_offset);
  if (!F2fsCrcValid(crc, cp_block, static_cast<uint32_t>(crc_offset))) {
    delete reinterpret_cast<Block *>(cp_page_1);
    delete reinterpret_cast<Block *>(cp_page_2);
    return nullptr;
  }

  cur_version = LeToCpu(cp_block->checkpoint_ver);

  if (cur_version == pre_version) {
    *version = cur_version;
    delete reinterpret_cast<Block *>(cp_page_2);
    return cp_page_1;
  }

  delete reinterpret_cast<Block *>(cp_page_2);
  delete reinterpret_cast<Block *>(cp_page_1);
  return nullptr;
}

zx_status_t FsckWorker::GetValidCheckpoint() {
  const SuperBlock *raw_sb = RawSuper(&sbi_);
  void *cp1, *cp2, *cur_page;
  uint64_t blk_size = sbi_.blocksize;
  uint64_t cp1_version = 0, cp2_version = 0;
  block_t cp_start_blk_no;
  Block *blk = new Block;

  if (sbi_.ckpt = reinterpret_cast<Checkpoint *>(blk); sbi_.ckpt == nullptr)
    return ZX_ERR_NO_MEMORY;

  // Finding out valid cp block involves read both
  // sets( cp pack1 and cp pack 2)
  cp_start_blk_no = LeToCpu(raw_sb->cp_blkaddr);
  cp1 = ValidateCheckpoint(cp_start_blk_no, &cp1_version);

  // The second checkpoint pack should start at the next segment
  cp_start_blk_no += 1 << LeToCpu(raw_sb->log_blocks_per_seg);
  cp2 = ValidateCheckpoint(cp_start_blk_no, &cp2_version);

  if (cp1 != nullptr && cp2 != nullptr) {
    if (VerAfter(cp2_version, cp1_version))
      cur_page = cp2;
    else
      cur_page = cp1;
  } else if (cp1 != nullptr) {
    cur_page = cp1;
  } else if (cp2 != nullptr) {
    cur_page = cp2;
  } else {
    delete reinterpret_cast<Block *>(cp1);
    delete reinterpret_cast<Block *>(cp2);
    delete blk;
    return ZX_ERR_INVALID_ARGS;
  }

  memcpy(sbi_.ckpt, cur_page, blk_size);

  delete reinterpret_cast<Block *>(cp1);
  delete reinterpret_cast<Block *>(cp2);
  return ZX_OK;
}

zx_status_t FsckWorker::SanityCheckCkpt() {
  unsigned int total, fsmeta;
  const SuperBlock *raw_super = RawSuper(&sbi_);
  Checkpoint *ckpt = GetCheckpoint(&sbi_);

  total = LeToCpu(raw_super->segment_count);
  fsmeta = LeToCpu(raw_super->segment_count_ckpt);
  fsmeta += LeToCpu(raw_super->segment_count_sit);
  fsmeta += LeToCpu(raw_super->segment_count_nat);
  fsmeta += LeToCpu(ckpt->rsvd_segment_count);
  fsmeta += LeToCpu(raw_super->segment_count_ssa);

  if (fsmeta >= total)
    return ZX_ERR_INVALID_ARGS;

  return ZX_OK;
}

zx_status_t FsckWorker::InitNodeManager() {
  const SuperBlock *sb_raw = RawSuper(&sbi_);
  NmInfo *nm_i = GetNmInfo(&sbi_);
  unsigned char *version_bitmap;
  unsigned int nat_segs, nat_blocks;

  nm_i->nat_blkaddr = LeToCpu(sb_raw->nat_blkaddr);

  // segment_count_nat includes pair segment so divide to 2.
  nat_segs = LeToCpu(sb_raw->segment_count_nat) >> 1;
  nat_blocks = nat_segs << LeToCpu(sb_raw->log_blocks_per_seg);
  nm_i->max_nid = kNatEntryPerBlock * nat_blocks;
  nm_i->fcnt = 0;
  nm_i->nat_cnt = 0;
  nm_i->init_scan_nid = LeToCpu(sbi_.ckpt->next_free_nid);
  nm_i->next_scan_nid = LeToCpu(sbi_.ckpt->next_free_nid);

  nm_i->bitmap_size = BitmapSize(&sbi_, MetaBitmap::kNatBitmap);

  if (nm_i->nat_bitmap = new char[nm_i->bitmap_size]; nm_i->nat_bitmap == nullptr)
    return ZX_ERR_NO_MEMORY;
  if (version_bitmap = static_cast<uint8_t *>(BitmapPrt(&sbi_, MetaBitmap::kNatBitmap));
      version_bitmap == nullptr)
    return ZX_ERR_NO_MEMORY;

  // copy version bitmap
  memcpy(nm_i->nat_bitmap, version_bitmap, nm_i->bitmap_size);
  return ZX_OK;
}

zx_status_t FsckWorker::BuildNodeManager() {
  if (sbi_.nm_info = new NmInfo; sbi_.nm_info == nullptr)
    return ZX_ERR_NO_MEMORY;

  if (zx_status_t err = InitNodeManager(); err != ZX_OK)
    return err;

  return ZX_OK;
}

zx_status_t FsckWorker::BuildSitInfo() {
  const SuperBlock *raw_sb = RawSuper(&sbi_);
  Checkpoint *ckpt = GetCheckpoint(&sbi_);
  SitInfo *sit_i;
  unsigned int sit_segs, start;
  char *src_bitmap, *dst_bitmap;
  unsigned int bitmap_size;

  if (sit_i = new SitInfo; sit_i == nullptr)
    return ZX_ERR_NO_MEMORY;

  GetSmInfo(&sbi_)->SitInfo = sit_i;

  sit_i->sentries = new SegEntry[TotalSegs(&sbi_)]();

  for (start = 0; start < TotalSegs(&sbi_); start++) {
    sit_i->sentries[start].cur_valid_map = new uint8_t[kSitVBlockMapSize]();
    sit_i->sentries[start].ckpt_valid_map = new uint8_t[kSitVBlockMapSize]();
    if (sit_i->sentries[start].cur_valid_map == nullptr ||
        sit_i->sentries[start].ckpt_valid_map == nullptr)
      return ZX_ERR_NO_MEMORY;
  }

  sit_segs = LeToCpu(raw_sb->segment_count_sit) >> 1;
  bitmap_size = BitmapSize(&sbi_, MetaBitmap::kSitBitmap);
  if (src_bitmap = static_cast<char *>(BitmapPrt(&sbi_, MetaBitmap::kSitBitmap));
      src_bitmap == nullptr)
    return ZX_ERR_NO_MEMORY;

  if (dst_bitmap = new char[bitmap_size]; dst_bitmap == nullptr)
    return ZX_ERR_NO_MEMORY;

  memcpy(dst_bitmap, src_bitmap, bitmap_size);

  sit_i->sit_base_addr = LeToCpu(raw_sb->sit_blkaddr);
  sit_i->sit_blocks = sit_segs << sbi_.log_blocks_per_seg;
  sit_i->written_valid_blocks = LeToCpu(static_cast<uint32_t>(ckpt->valid_block_count));
  sit_i->sit_bitmap = dst_bitmap;
  sit_i->bitmap_size = bitmap_size;
  sit_i->dirty_sentries = 0;
  sit_i->sents_per_block = kSitEntryPerBlock;
  sit_i->elapsed_time = LeToCpu(ckpt->elapsed_time);
  return ZX_OK;
}

void FsckWorker::ResetCurseg(CursegType type, int modified) {
  CursegInfo *curseg = SegMgr::CURSEG_I(&sbi_, type);

  curseg->segno = curseg->next_segno;
  curseg->zone = GetZoneNoFromSegNo(&sbi_, curseg->segno);
  curseg->next_blkoff = 0;
  curseg->next_segno = kNullSegNo;
}

zx_status_t FsckWorker::ReadCompactedSummaries() {
  Checkpoint *ckpt = GetCheckpoint(&sbi_);
  CursegInfo *curseg;
  block_t start;
  Block *blk = new Block;
  uint32_t j, offset;

  start = StartSumBlock();

  ReadBlock(blk->data, start++);

  curseg = SegMgr::CURSEG_I(&sbi_, CursegType::kCursegHotData);
  memcpy(&curseg->sum_blk->n_nats, blk->data, kSumJournalSize);

  curseg = SegMgr::CURSEG_I(&sbi_, CursegType::kCursegColdData);
  memcpy(&curseg->sum_blk->n_sits, blk->data + kSumJournalSize, kSumJournalSize);

  offset = 2 * kSumJournalSize;
  for (int32_t i = static_cast<int32_t>(CursegType::kCursegHotData);
       i <= CursegType::kCursegColdData; i++) {
    unsigned short blk_off;
    unsigned int segno;

    curseg = SegMgr::CURSEG_I(&sbi_, static_cast<CursegType>(i));
    segno = LeToCpu(ckpt->cur_data_segno[i]);
    blk_off = LeToCpu(ckpt->cur_data_blkoff[i]);
    curseg->next_segno = segno;
    ResetCurseg(static_cast<CursegType>(i), 0);
    curseg->alloc_type = ckpt->alloc_type[i];
    curseg->next_blkoff = blk_off;

    if (curseg->alloc_type == static_cast<uint8_t>(AllocMode::kSSR))
      blk_off = static_cast<unsigned short>(sbi_.blocks_per_seg);

    for (j = 0; j < blk_off; j++) {
      Summary *s;
      s = (Summary *)(blk->data + offset);
      curseg->sum_blk->entries[j] = *s;
      offset += kSummarySize;
      if (offset + kSummarySize <= kPageCacheSize - kSumFooterSize)
        continue;
      memset(blk->data, 0, kPageSize);
      ReadBlock(blk->data, start++);
      offset = 0;
    }
  }

  delete blk;
  return ZX_OK;
}

zx_status_t FsckWorker::RestoreNodeSummary(unsigned int segno, SummaryBlock *sum_blk) {
  Node *node_blk;
  Summary *sum_entry;
  block_t addr;
  uint32_t i;
  Block *blk = new Block;

  if (blk == nullptr)
    return ZX_ERR_NO_MEMORY;

  // scan the node segment
  addr = StartBlock(&sbi_, segno);
  sum_entry = &sum_blk->entries[0];
  for (i = 0; i < sbi_.blocks_per_seg; i++, sum_entry++) {
    if (ReadBlock(blk->data, addr))
      break;
    node_blk = reinterpret_cast<Node *>(blk->data);
    sum_entry->nid = node_blk->footer.nid;
    addr++;
  }
  delete blk;
  return ZX_OK;
}

zx_status_t FsckWorker::ReadNormalSummaries(CursegType type) {
  Checkpoint *ckpt = GetCheckpoint(&sbi_);
  SummaryBlock *sum_blk;
  CursegInfo *curseg;
  unsigned short blk_off;
  unsigned int segno = 0;
  block_t blk_addr = 0;

  if (IsDataSeg(type)) {
    segno = LeToCpu(ckpt->cur_data_segno[static_cast<int>(type)]);
    blk_off = LeToCpu(ckpt->cur_data_blkoff[type - CursegType::kCursegHotData]);

    if (IsSetCkptFlags(ckpt, kCpUmountFlag))
      blk_addr = SumBlkAddr(kNrCursegType, static_cast<int>(type));
    else
      blk_addr = SumBlkAddr(kNrCursegDataType, static_cast<int>(type));
  } else {
    segno = LeToCpu(ckpt->cur_node_segno[type - CursegType::kCursegHotNode]);
    blk_off = LeToCpu(ckpt->cur_node_blkoff[type - CursegType::kCursegHotNode]);

    if (IsSetCkptFlags(ckpt, kCpUmountFlag))
      blk_addr = SumBlkAddr(kNrCursegNodeType, type - CursegType::kCursegHotNode);
    else
      blk_addr = GetSumBlock(&sbi_, segno);
  }

  sum_blk = reinterpret_cast<SummaryBlock *>(new Block);
  ReadBlock(sum_blk, blk_addr);

  if (IsNodeSeg(type)) {
    if (IsSetCkptFlags(ckpt, kCpUmountFlag)) {
#if 0  // do not change original value
      Summary *sum_entry = &sum_blk->entries[0];
      for (uint64_t i = 0; i < sbi->blocks_per_seg; i++, sum_entry++) {
				sum_entry->version = 0;
				sum_entry->ofs_in_node = 0;
      }
#endif
    } else {
      if (zx_status_t ret = RestoreNodeSummary(segno, sum_blk); ret != ZX_OK) {
        delete reinterpret_cast<Block *>(sum_blk);
        return ret;
      }
    }
  }

  curseg = SegMgr::CURSEG_I(&sbi_, type);
  memcpy(curseg->sum_blk, sum_blk, kPageCacheSize);
  curseg->next_segno = segno;
  ResetCurseg(type, 0);
  curseg->alloc_type = ckpt->alloc_type[static_cast<int>(type)];
  curseg->next_blkoff = blk_off;
  delete reinterpret_cast<Block *>(sum_blk);

  return ZX_OK;
}

zx_status_t FsckWorker::RestoreCursegSummaries() {
  int32_t type = static_cast<int32_t>(CursegType::kCursegHotData);

  if (IsSetCkptFlags(GetCheckpoint(&sbi_), kCpCompactSumFlag)) {
    if (zx_status_t ret = ReadCompactedSummaries(); ret != ZX_OK)
      return ret;
    type = static_cast<int32_t>(CursegType::kCursegHotNode);
  }

  for (; type <= CursegType::kCursegColdNode; type++) {
    if (zx_status_t ret = ReadNormalSummaries(static_cast<CursegType>(type)); ret != ZX_OK)
      return ret;
  }
  return ZX_OK;
}

zx_status_t FsckWorker::BuildCurseg() {
  CursegInfo *array;
  int i;

  if (array = new CursegInfo[kNrCursegType](); array == nullptr)
    return ZX_ERR_NO_MEMORY;

  GetSmInfo(&sbi_)->curseg_array = array;

  for (i = 0; i < kNrCursegType; i++) {
    array[i].sum_blk = reinterpret_cast<SummaryBlock *>(new Block);
    if (!array[i].sum_blk)
      return ZX_ERR_NO_MEMORY;
    array[i].segno = kNullSegNo;
    array[i].next_blkoff = 0;
  }
  return RestoreCursegSummaries();
}

inline void FsckWorker::ChkSegRange(unsigned int segno) {
  unsigned int end_segno = GetSmInfo(&sbi_)->segment_count - 1;
  ZX_ASSERT(segno <= end_segno);
}

SitBlock *FsckWorker::GetCurrentSitPage(unsigned int segno) {
  SitInfo *sit_i = GetSitInfo(&sbi_);
  unsigned int offset = SitBlockOffset(sit_i, segno);
  block_t blk_addr = sit_i->sit_base_addr + offset;
  SitBlock *sit_blk = reinterpret_cast<SitBlock *>(new Block);

  ChkSegRange(segno);

  // calculate sit block address
  if (TestValidBitmap(offset, sit_i->sit_bitmap))
    blk_addr += sit_i->sit_blocks;

  ReadBlock(sit_blk, blk_addr);

  return sit_blk;
}

void FsckWorker::CheckBlockCount(uint32_t segno, SitEntry *raw_sit) {
  SmInfo *sm_info = GetSmInfo(&sbi_);
  uint32_t end_segno = sm_info->segment_count - 1;
  int valid_blocks = 0;

  // check segment usage
  ZX_ASSERT(GetSitVblocks(raw_sit) <= sbi_.blocks_per_seg);

  // check boundary of a given segment number
  ZX_ASSERT(segno <= end_segno);

  // check bitmap with valid block count
  for (uint64_t i = 0; i < sbi_.blocks_per_seg; i++)
    if (TestValidBitmap(i, (char *)raw_sit->valid_map))
      valid_blocks++;
  ZX_ASSERT(GetSitVblocks(raw_sit) == valid_blocks);
}

void FsckWorker::SegInfoFromRawSit(SegEntry *se, SitEntry *raw_sit) {
  se->valid_blocks = GetSitVblocks(raw_sit);
  se->ckpt_valid_blocks = GetSitVblocks(raw_sit);
  memcpy(se->cur_valid_map, raw_sit->valid_map, kSitVBlockMapSize);
  memcpy(se->ckpt_valid_map, raw_sit->valid_map, kSitVBlockMapSize);
  se->type = GetSitType(raw_sit);
  se->mtime = LeToCpu(raw_sit->mtime);
}

SegEntry *FsckWorker::GetSegEntry(unsigned int segno) {
  SitInfo *sit_i = GetSitInfo(&sbi_);
  return &sit_i->sentries[segno];
}

SegType FsckWorker::GetSumBlockInfo(uint32_t segno, SummaryBlock *sum_blk) {
  Checkpoint *ckpt = GetCheckpoint(&sbi_);
  CursegInfo *curseg;
  int ret;
  uint64_t ssa_blk;

  ssa_blk = GetSumBlock(&sbi_, segno);
  for (int type = 0; type < kNrCursegNodeType; type++) {
    if (segno == ckpt->cur_node_segno[type]) {
      curseg = SegMgr::CURSEG_I(&sbi_, CursegType::kCursegHotNode + type);
      memcpy(sum_blk, curseg->sum_blk, kBlockSize);
      return SegType::kSegTypeCurNode;  // current node seg was not stored
    }
  }

  for (int type = 0; type < kNrCursegDataType; type++) {
    if (segno == ckpt->cur_data_segno[type]) {
      curseg = SegMgr::CURSEG_I(&sbi_, CursegType::kCursegHotData + type);
      memcpy(sum_blk, curseg->sum_blk, kBlockSize);
      ZX_ASSERT(!IsSumNodeSeg(sum_blk->footer));
#ifdef F2FS_BU_DEBUG
      // TODO: DBG (2)
      printf("segno [0x%x] is current data seg[0x%x]\n", segno, type);
#endif
      return SegType::kSegTypeCurData;  // current data seg was not stored
    }
  }

  ret = ReadBlock(sum_blk, ssa_blk);
  ZX_ASSERT(ret >= 0);

  if (IsSumNodeSeg(sum_blk->footer))
    return SegType::kSegTypeNode;
  else
    return SegType::kSegTypeData;
}

uint32_t FsckWorker::GetSegNo(uint32_t blk_addr) {
  return (uint32_t)(BlkoffFromMain(&sbi_, blk_addr) >> sbi_.log_blocks_per_seg);
}

SegType FsckWorker::GetSumEntry(uint32_t blk_addr, Summary *sum_entry) {
  uint32_t segno, offset;
  Block *blk = new Block;

  segno = GetSegNo(blk_addr);
  offset = OffsetInSeg(&sbi_, blk_addr);

  SummaryBlock *sum_blk = reinterpret_cast<SummaryBlock *>(blk->data);
  SegType type = GetSumBlockInfo(segno, sum_blk);
  memcpy(sum_entry, &(sum_blk->entries[offset]), sizeof(Summary));
  delete blk;
  return type;
}

zx_status_t FsckWorker::GetNatEntry(nid_t nid, RawNatEntry *raw_nat) {
  FsckInfo *fsck = &fsck_;
  NmInfo *nm_i = GetNmInfo(&sbi_);
  pgoff_t block_off;
  pgoff_t block_addr;
  pgoff_t seg_off;
  int entry_off;
  int ret;

  if ((nid / kNatEntryPerBlock) > fsck->nr_nat_entries) {
    FX_LOGS(WARNING) << "nid is over max nid";
    return ZX_ERR_INVALID_ARGS;
  }

  if (auto i_or = LookupNatInJournal(nid, raw_nat); i_or.is_ok())
    return ZX_OK;

  Block *blk = new Block;
  NatBlock *nat_block = reinterpret_cast<NatBlock *>(blk);

  block_off = nid / kNatEntryPerBlock;
  entry_off = nid % kNatEntryPerBlock;

  seg_off = block_off >> sbi_.log_blocks_per_seg;
  block_addr = (pgoff_t)(nm_i->nat_blkaddr + (seg_off << sbi_.log_blocks_per_seg << 1) +
                         (block_off & ((1 << sbi_.log_blocks_per_seg) - 1)));

  if (TestValidBitmap(block_off, nm_i->nat_bitmap))
    block_addr += sbi_.blocks_per_seg;

  ret = ReadBlock(nat_block, block_addr);
  ZX_ASSERT(ret >= 0);

  memcpy(raw_nat, &nat_block->entries[entry_off], sizeof(RawNatEntry));
  delete blk;
  return ZX_OK;
}

zx_status_t FsckWorker::GetNodeInfo(nid_t nid, NodeInfo *ni) {
  RawNatEntry raw_nat;
  zx_status_t ret = GetNatEntry(nid, &raw_nat);
  ni->nid = nid;
  NodeInfoFromRawNat(ni, &raw_nat);
  return ret;
}

void FsckWorker::BuildSitEntries() {
  SitInfo *sit_i = GetSitInfo(&sbi_);
  CursegInfo *curseg = SegMgr::CURSEG_I(&sbi_, CursegType::kCursegColdData);
  SummaryBlock *sum = curseg->sum_blk;
  unsigned int segno;

  for (segno = 0; segno < TotalSegs(&sbi_); segno++) {
    SegEntry *se = &sit_i->sentries[segno];
    SitBlock *sit_blk;
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
      sit_blk = GetCurrentSitPage(segno);
      sit = sit_blk->entries[SitEntryOffset(sit_i, segno)];
      delete reinterpret_cast<Block *>(sit_blk);
    }
    CheckBlockCount(segno, &sit);
    SegInfoFromRawSit(se, &sit);
  }
}

zx_status_t FsckWorker::BuildSegmentManager() {
  const SuperBlock *raw_super = RawSuper(&sbi_);
  Checkpoint *ckpt = GetCheckpoint(&sbi_);
  SmInfo *sm_info;

  if (sm_info = new SmInfo(); sm_info == nullptr)
    return ZX_ERR_NO_MEMORY;

  // init sm info
  sbi_.sm_info = sm_info;
  sm_info->seg0_blkaddr = LeToCpu(raw_super->segment0_blkaddr);
  sm_info->main_blkaddr = LeToCpu(raw_super->main_blkaddr);
  sm_info->segment_count = LeToCpu(raw_super->segment_count);
  sm_info->reserved_segments = LeToCpu(ckpt->rsvd_segment_count);
  sm_info->ovp_segments = LeToCpu(ckpt->overprov_segment_count);
  sm_info->main_segments = LeToCpu(raw_super->segment_count_main);
  sm_info->ssa_blkaddr = LeToCpu(raw_super->ssa_blkaddr);

  if (zx_status_t ret = BuildSitInfo(); ret != ZX_OK)
    return ret;
  if (zx_status_t ret = BuildCurseg(); ret != ZX_OK)
    return ret;
  BuildSitEntries();
  return ZX_OK;
}

void FsckWorker::BuildSitAreaBitmap() {
  FsckInfo *fsck = &fsck_;
  SmInfo *sm_i = GetSmInfo(&sbi_);
  uint32_t sum_vblocks = 0;
  uint32_t free_segs = 0;
  uint32_t vblocks = 0;

  fsck->sit_area_bitmap_sz = sm_i->main_segments * kSitVBlockMapSize;
  fsck->sit_area_bitmap = new char[fsck->sit_area_bitmap_sz];
  ZX_ASSERT(fsck->sit_area_bitmap_sz == fsck->main_area_bitmap_sz);
  memset(fsck->sit_area_bitmap, 0, fsck->sit_area_bitmap_sz);
  char *ptr = fsck->sit_area_bitmap;

  for (uint32_t segno = 0; segno < sm_i->main_segments; segno++) {
    SegEntry *se = GetSegEntry(segno);

    memcpy(ptr, se->cur_valid_map, kSitVBlockMapSize);
    ptr += kSitVBlockMapSize;
    vblocks = 0;
    for (uint64_t j = 0; j < kSitVBlockMapSize; j++) {
      vblocks += std::bitset<8>(se->cur_valid_map[j]).count();
    }
    ZX_ASSERT(vblocks == se->valid_blocks);

    if (se->valid_blocks == 0x0) {
      if (sbi_.ckpt->cur_node_segno[0] == segno || sbi_.ckpt->cur_data_segno[0] == segno ||
          sbi_.ckpt->cur_node_segno[1] == segno || sbi_.ckpt->cur_data_segno[1] == segno ||
          sbi_.ckpt->cur_node_segno[2] == segno || sbi_.ckpt->cur_data_segno[2] == segno) {
        continue;
      } else {
        free_segs++;
      }
    } else {
      ZX_ASSERT(se->valid_blocks <= 512);
      sum_vblocks += se->valid_blocks;
    }
  }

  fsck->chk.sit_valid_blocks = sum_vblocks;
  fsck->chk.sit_free_segs = free_segs;
#ifdef F2FS_BU_DEBUG
  // TODO: DBG (1)
  printf("Blocks [0x%x : %d] Free Segs [0x%x : %d]\n\n", sum_vblocks, sum_vblocks, free_segs,
         free_segs);
#endif
}

zx::status<int> FsckWorker::LookupNatInJournal(uint32_t nid, RawNatEntry *raw_nat) {
  CursegInfo *curseg = SegMgr::CURSEG_I(&sbi_, CursegType::kCursegHotData);
  SummaryBlock *sum = curseg->sum_blk;
  int i = 0;

  for (i = 0; i < NatsInCursum(sum); i++) {
    if (LeToCpu(NidInJournal(sum, i)) == nid) {
      RawNatEntry ret = NatInJournal(sum, i);
      memcpy(raw_nat, &ret, sizeof(RawNatEntry));
#ifdef F2FS_BU_DEBUG
      // TODO: DBG (3)
      printf("==> Found nid [0x%x] in nat cache\n", nid);
#endif
      return zx::ok(i);
    }
  }
  return zx::error(ZX_ERR_NOT_FOUND);
}

void FsckWorker::BuildNatAreaBitmap() {
  FsckInfo *fsck = &fsck_;
  const SuperBlock *raw_sb = RawSuper(&sbi_);
  NmInfo *nm_i = GetNmInfo(&sbi_);
  NatBlock *nat_block;
  uint32_t nid, nr_nat_blks;

  pgoff_t block_off;
  pgoff_t block_addr;
  pgoff_t seg_off;
  int ret;

  Block *blk = new Block;
  nat_block = reinterpret_cast<NatBlock *>(blk);

  // Alloc & build nat entry bitmap
  nr_nat_blks = (LeToCpu(raw_sb->segment_count_nat) / 2) << sbi_.log_blocks_per_seg;

  fsck->nr_nat_entries = nr_nat_blks * kNatEntryPerBlock;
  fsck->nat_area_bitmap_sz = (fsck->nr_nat_entries + 7) / 8;
  fsck->nat_area_bitmap = new char[fsck->nat_area_bitmap_sz];
  ZX_ASSERT(fsck->nat_area_bitmap != nullptr);
  memset(fsck->nat_area_bitmap, 0, fsck->nat_area_bitmap_sz);

  for (block_off = 0; block_off < nr_nat_blks; block_off++) {
    seg_off = block_off >> sbi_.log_blocks_per_seg;
    block_addr = (pgoff_t)(nm_i->nat_blkaddr + (seg_off << sbi_.log_blocks_per_seg << 1) +
                           (block_off & ((1 << sbi_.log_blocks_per_seg) - 1)));

    if (TestValidBitmap(block_off, nm_i->nat_bitmap))
      block_addr += sbi_.blocks_per_seg;

    ret = ReadBlock(nat_block, block_addr);
    ZX_ASSERT(ret >= 0);

    nid = static_cast<uint32_t>(block_off * kNatEntryPerBlock);
    for (uint32_t i = 0; i < kNatEntryPerBlock; i++) {
      RawNatEntry raw_nat;
      NodeInfo ni;
      ni.nid = nid + i;

      if ((nid + i) == NodeIno(&sbi_) || (nid + i) == MetaIno(&sbi_)) {
        ZX_ASSERT(nat_block->entries[i].block_addr != 0x0);
        continue;
      }

      if (auto i_or = LookupNatInJournal(nid + i, &raw_nat); i_or.is_ok()) {
        NodeInfoFromRawNat(&ni, &raw_nat);
        if (ni.blk_addr != kNullAddr) {
          SetValidBitmap(nid + i, fsck->nat_area_bitmap);
          fsck->chk.valid_nat_entry_cnt++;
#ifdef F2FS_BU_DEBUG
          // TODO: DBG (3)
          printf("nid[0x%x] in nat cache\n", nid + i);
#endif
        }
      } else {
        NodeInfoFromRawNat(&ni, &nat_block->entries[i]);
        if (ni.blk_addr != kNullAddr) {
          ZX_ASSERT(nid + i != 0x0);
#ifdef F2FS_BU_DEBUG
          // TODO: DBG (3)
          printf("nid[0x%8x] in nat entry [0x%16x] [0x%8x]\n", nid + i, ni.blk_addr, ni.ino);
#endif
          SetValidBitmap(nid + i, fsck->nat_area_bitmap);
          fsck->chk.valid_nat_entry_cnt++;
        }
      }
    }
  }
  delete blk;
#ifdef F2FS_BU_DEBUG
  // TODO: DBG (1)
  printf("valid nat entries (block_addr != 0x0) [0x%8x : %u]\n", fsck->chk.valid_nat_entry_cnt,
         fsck->chk.valid_nat_entry_cnt);
#endif
}

zx_status_t FsckWorker::DoMount() {
  zx_status_t ret;
  sbi_.active_logs = kNrCursegType;

  if (ret = ValidateSuperblock(0); ret != ZX_OK) {
    if (ret = ValidateSuperblock(1); ret != ZX_OK) {
      return ret;
    }
  }

  PrintRawSbInfo();
  InitSbInfo();

  if (ret = GetValidCheckpoint(); ret != ZX_OK) {
    FX_LOGS(ERROR) << "Can't find valid checkpoint" << ret;
    return ret;
  }
  if (ret = SanityCheckCkpt(); ret != ZX_OK) {
    FX_LOGS(ERROR) << "Checkpoint is polluted" << ret;
    return ret;
  }

  PrintCkptInfo();
  sbi_.total_valid_node_count = LeToCpu(sbi_.ckpt->valid_node_count);
  sbi_.total_valid_inode_count = LeToCpu(sbi_.ckpt->valid_inode_count);
  sbi_.user_block_count = LeToCpu(static_cast<block_t>(sbi_.ckpt->user_block_count));
  sbi_.total_valid_block_count = LeToCpu(static_cast<block_t>(sbi_.ckpt->valid_block_count));
  sbi_.last_valid_block_count = sbi_.total_valid_block_count;
  sbi_.alloc_valid_block_count = 0;

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
  SitInfo *sit_i = GetSitInfo(&sbi_);
  SmInfo *sm_i = GetSmInfo(&sbi_);
  NmInfo *nm_i = GetNmInfo(&sbi_);

  // free nm_info
  delete[] nm_i->nat_bitmap;
  delete sbi_.nm_info;

  // free sit_info
  for (uint32_t i = 0; i < TotalSegs(&sbi_); i++) {
    delete[] sit_i->sentries[i].cur_valid_map;
    delete[] sit_i->sentries[i].ckpt_valid_map;
  }
  delete[] sit_i->sentries;

  delete[] sit_i->sit_bitmap;
  delete sm_i->SitInfo;

  // free sm_info
  for (uint32_t i = 0; i < kNrCursegType; i++)
    delete reinterpret_cast<Block *>(sm_i->curseg_array[i].sum_blk);

  delete[] sm_i->curseg_array;
  delete sbi_.sm_info;

  delete reinterpret_cast<Block *>(sbi_.ckpt);
  delete sbi_.raw_super;
}

zx_status_t FsckWorker::DoFsck() {
  uint32_t blk_cnt;
  int ret = ZX_OK;
  if (ret = Init(); ret != ZX_OK)
    return ret;

  ChkOrphanNode();
  FX_LOGS(INFO) << "checking orphan node.. done";

  // Travses all block recursively from root inode
  blk_cnt = 1;
  ret = ChkNodeBlk(nullptr, sbi_.root_ino_num, FileType::kFtDir, NodeType::kTypeInode, &blk_cnt);
  FX_LOGS(INFO) << "checking node blocks.. done: " << ret;
  if (ret != ZX_OK) {
    Free();
    return ret;
  }

  ret = Verify();
  FX_LOGS(INFO) << "verifying.. done: " << ret;
  Free();
  return ret;
}

zx_status_t FsckWorker::Run() {
  zx_status_t ret = ZX_OK;
  if (ret = DoMount(); ret != ZX_OK)
    return ret;

  ret = DoFsck();
#if 0  // porting needed
  // ret = DoDump(sbi);
#endif
  DoUmount();
  FX_LOGS(INFO) << "Fsck.. done: " << ret;
  return ret;
}

}  // namespace f2fs::fsck
