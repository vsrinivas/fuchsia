// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/f2fs/f2fs.h"

namespace f2fs {
void NodePage::FillNodeFooter(nid_t nid, nid_t ino, uint32_t ofs, bool reset) {
  Node &rn = GetRawNode();
  if (reset)
    memset(&rn, 0, sizeof(rn));
  rn.footer.nid = CpuToLe(nid);
  rn.footer.ino = CpuToLe(ino);
  rn.footer.flag = CpuToLe(ofs << static_cast<int>(BitShift::kOffsetBitShift));
}

void NodePage::CopyNodeFooterFrom(NodePage &src) {
  memcpy(&GetRawNode().footer, &src.GetRawNode().footer, sizeof(NodeFooter));
}

void NodePage::FillNodeFooterBlkaddr(block_t blkaddr) {
  Checkpoint &ckpt = fs()->GetSuperblockInfo().GetCheckpoint();
  GetRawNode().footer.cp_ver = ckpt.checkpoint_ver;
  GetRawNode().footer.next_blkaddr = blkaddr;
}

nid_t NodePage::InoOfNode() { return LeToCpu(GetRawNode().footer.ino); }

nid_t NodePage::NidOfNode() { return LeToCpu(GetRawNode().footer.nid); }

uint32_t NodePage::OfsOfNode() {
  uint32_t flag = LeToCpu(GetRawNode().footer.flag);
  return flag >> static_cast<int>(BitShift::kOffsetBitShift);
}

uint64_t NodePage::CpverOfNode() { return LeToCpu(GetRawNode().footer.cp_ver); }

block_t NodePage::NextBlkaddrOfNode() { return LeToCpu(GetRawNode().footer.next_blkaddr); }

// f2fs assigns the following node offsets described as (num).
// N = kNidsPerBlock
//
//  Inode block (0)
//    |- direct node (1)
//    |- direct node (2)
//    |- indirect node (3)
//    |            `- direct node (4 => 4 + N - 1)
//    |- indirect node (4 + N)
//    |            `- direct node (5 + N => 5 + 2N - 1)
//    `- double indirect node (5 + 2N)
//                 `- indirect node (6 + 2N)
//                       `- direct node (x(N + 1))
bool NodePage::IsDnode() {
  uint32_t ofs = OfsOfNode();
  if (ofs == kOfsIndirectNode1 || ofs == kOfsIndirectNode2 || ofs == kOfsDoubleIndirectNode)
    return false;
  if (ofs >= kOfsDoubleIndirectNode + 1) {
    ofs -= kOfsDoubleIndirectNode + 1;
    if (static_cast<int64_t>(ofs) % (kNidsPerBlock + 1))
      return false;
  }
  return true;
}

void NodePage::SetNid(size_t off, nid_t nid, bool is_inode) {
  WaitOnWriteback();

  if (is_inode) {
    GetRawNode().i.i_nid[off - kNodeDir1Block] = CpuToLe(nid);
  } else {
    GetRawNode().in.nid[off] = CpuToLe(nid);
  }
}

nid_t NodePage::GetNid(size_t off, bool is_inode) {
  if (is_inode) {
    return LeToCpu(GetRawNode().i.i_nid[off - kNodeDir1Block]);
  }
  return LeToCpu(GetRawNode().in.nid[off]);
}

bool NodePage::IsColdNode() {
  uint32_t flag = LeToCpu(GetRawNode().footer.flag);
  return TestBit(static_cast<uint32_t>(BitShift::kColdBitShift), &flag);
}

bool NodePage::IsFsyncDnode() {
  uint32_t flag = LeToCpu(GetRawNode().footer.flag);
  return TestBit(static_cast<uint32_t>(BitShift::kFsyncBitShift), &flag);
}

bool NodePage::IsDentDnode() {
  uint32_t flag = LeToCpu(GetRawNode().footer.flag);
  return TestBit(static_cast<uint32_t>(BitShift::kDentBitShift), &flag);
}

void NodePage::SetColdNode(VnodeF2fs &vnode) {
  uint32_t flag = LeToCpu(GetRawNode().footer.flag);

  if (vnode.IsDir()) {
    ClearBit(static_cast<uint32_t>(BitShift::kColdBitShift), &flag);
  } else {
    SetBit(static_cast<uint32_t>(BitShift::kColdBitShift), &flag);
  }
  GetRawNode().footer.flag = CpuToLe(flag);
}

void NodePage::SetFsyncMark(bool mark) {
  uint32_t flag = LeToCpu(GetRawNode().footer.flag);
  if (mark) {
    SetBit(static_cast<uint32_t>(BitShift::kFsyncBitShift), &flag);
  } else {
    ClearBit(static_cast<uint32_t>(BitShift::kFsyncBitShift), &flag);
  }
  GetRawNode().footer.flag = CpuToLe(flag);
}

void NodePage::SetDentryMark(bool mark) {
  uint32_t flag = LeToCpu(GetRawNode().footer.flag);
  if (mark) {
    SetBit(static_cast<uint32_t>(BitShift::kDentBitShift), &flag);
  } else {
    ClearBit(static_cast<uint32_t>(BitShift::kDentBitShift), &flag);
  }
  GetRawNode().footer.flag = CpuToLe(flag);
}

block_t NodePage::StartBidxOfNode(const VnodeF2fs &vnode) {
  uint32_t node_ofs = OfsOfNode(), NumOfIndirectNodes = 0;

  if (node_ofs == kOfsInode) {
    return 0;
  } else if (node_ofs <= kOfsDirectNode2) {
    NumOfIndirectNodes = 0;
  } else if (node_ofs >= kOfsIndirectNode1 && node_ofs < kOfsIndirectNode2) {
    NumOfIndirectNodes = 1;
  } else if (node_ofs >= kOfsIndirectNode2 && node_ofs < kOfsDoubleIndirectNode) {
    NumOfIndirectNodes = 2;
  } else {
    NumOfIndirectNodes = (node_ofs - kOfsDoubleIndirectNode - 2) / (kNidsPerBlock + 1);
  }

  uint32_t bidx = node_ofs - NumOfIndirectNodes - 1;
  return (vnode.GetAddrsPerInode() + safemath::CheckMul(bidx, kAddrsPerBlock)).ValueOrDie();
}
}  // namespace f2fs
