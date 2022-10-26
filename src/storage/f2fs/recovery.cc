// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/f2fs/f2fs.h"

namespace f2fs {

bool F2fs::SpaceForRollForward() const {
  SuperblockInfo &superblock_info = GetSuperblockInfo();
  std::lock_guard stat_lock(superblock_info.GetStatLock());
  return superblock_info.GetLastValidBlockCount() + superblock_info.GetAllocValidBlockCount() <=
         superblock_info.GetUserBlockCount();
}

F2fs::FsyncInodeEntry *F2fs::GetFsyncInode(FsyncInodeList &inode_list, nid_t ino) {
  auto inode_entry =
      inode_list.find_if([ino](const auto &entry) { return entry.GetVnode().Ino() == ino; });
  if (inode_entry == inode_list.end())
    return nullptr;
  return &(*inode_entry);
}

zx_status_t F2fs::RecoverDentry(NodePage &ipage, VnodeF2fs &vnode) {
  Node *raw_node = ipage.GetAddress<Node>();
  Inode *raw_inode = &(raw_node->i);
  fbl::RefPtr<VnodeF2fs> dir_refptr;
  zx_status_t err = ZX_OK;

  if (!ipage.IsDentDnode()) {
    return ZX_OK;
  }

  if (err = VnodeF2fs::Vget(this, LeToCpu(raw_inode->i_pino), &dir_refptr); err != ZX_OK) {
    return err;
  }

  return fbl::RefPtr<Dir>::Downcast(dir_refptr)->RecoverLink(vnode).status_value();
}

zx_status_t F2fs::RecoverInode(VnodeF2fs &vnode, NodePage &node_page) {
  struct Node *raw_node = node_page.GetAddress<Node>();
  struct Inode *raw_inode = &(raw_node->i);

  vnode.SetMode(LeToCpu(raw_inode->i_mode));
  vnode.SetSize(LeToCpu(raw_inode->i_size));
  vnode.SetATime(LeToCpu(raw_inode->i_atime), LeToCpu(raw_inode->i_atime_nsec));
  vnode.SetCTime(LeToCpu(raw_inode->i_ctime), LeToCpu(raw_inode->i_ctime_nsec));
  vnode.SetMTime(LeToCpu(raw_inode->i_mtime), LeToCpu(raw_inode->i_mtime_nsec));

  return RecoverDentry(node_page, vnode);
}

zx_status_t F2fs::FindFsyncDnodes(FsyncInodeList &inode_list) {
  SuperblockInfo &superblock_info = GetSuperblockInfo();
  uint64_t cp_ver = LeToCpu(superblock_info.GetCheckpoint().checkpoint_ver);
  fbl::RefPtr<VnodeF2fs> vnode_refptr;
  zx_status_t err = ZX_OK;

  // Retrieve the curseg information of kCursegWarmNode
  CursegInfo *curseg = segment_manager_->CURSEG_I(CursegType::kCursegWarmNode);
  // Get blkaddr from which it starts finding fsynced dnode block
  block_t blkaddr = segment_manager_->StartBlock(curseg->segno) + curseg->next_blkoff;

  fbl::RefPtr<NodePage> inode_page;
  while (true) {
    LockedPage page;
    // Since node inode cache cannot be used for recovery, use meta inode cache temporarily and
    // delete it later. Meta vnode is indexed by LBA, it can be used to read node blocks. This
    // method eliminates duplicate node block reads.
    if (zx_status_t ret = GetMetaPage(blkaddr, &page); ret != ZX_OK) {
      return ret;
    }

    if (cp_ver != page.GetPage<NodePage>().CpverOfNode()) {
      break;
    }

    // Reserve inode page
    if (IsInode(*page)) {
      inode_page = fbl::RefPtr<NodePage>::Downcast(page.CopyRefPtr());
    }

    if (!page.GetPage<NodePage>().IsFsyncDnode()) {
      // Check next segment
      blkaddr = page.GetPage<NodePage>().NextBlkaddrOfNode();
      page->ClearUptodate();
      continue;
    }

    auto entry_ptr = GetFsyncInode(inode_list, page.GetPage<NodePage>().InoOfNode());
    if (entry_ptr) {
      entry_ptr->SetLastDnodeBlkaddr(blkaddr);
      if (inode_page && inode_page->IsDentDnode()) {
        entry_ptr->GetVnode().SetFlag(InodeInfoFlag::kIncLink);
      }
    } else {
      // Recover reserved inode page
      ZX_DEBUG_ASSERT(inode_page);
      ZX_DEBUG_ASSERT(inode_page->InoOfNode() == page.GetPage<NodePage>().InoOfNode());

      if (inode_page->IsDentDnode()) {
        if (err = GetNodeManager().RecoverInodePage(*inode_page); err != ZX_OK) {
          break;
        }
      }

      if (err = VnodeF2fs::Vget(this, page.GetPage<NodePage>().InoOfNode(), &vnode_refptr);
          err != ZX_OK) {
        break;
      }

      // Add this fsync inode to the list
      auto entry = std::make_unique<FsyncInodeEntry>(std::move(vnode_refptr));
      entry_ptr = entry.get();
      entry->SetLastDnodeBlkaddr(blkaddr);
      inode_list.push_back(std::move(entry));
    }
    if (inode_page) {
      ZX_DEBUG_ASSERT(inode_page->InoOfNode() == page.GetPage<NodePage>().InoOfNode());
      if (err = RecoverInode(entry_ptr->GetVnode(), *inode_page); err != ZX_OK) {
        break;
      }
    }

    // Get the next block information from footer
    blkaddr = page.GetPage<NodePage>().NextBlkaddrOfNode();
    page->ClearUptodate();
    inode_page.reset();
  }
  return err;
}

void F2fs::DestroyFsyncDnodes(FsyncInodeList &inode_list) {
  while (!inode_list.is_empty()) {
    inode_list.pop_front();
  }
}

void F2fs::CheckIndexInPrevNodes(block_t blkaddr) {
  SuperblockInfo &superblock_info = GetSuperblockInfo();
  uint32_t segno = segment_manager_->GetSegmentNumber(blkaddr);
  uint16_t blkoff = static_cast<uint16_t>(segment_manager_->GetSegOffFromSeg0(blkaddr) &
                                          (superblock_info.GetBlocksPerSeg() - 1));
  Summary sum;
  nid_t ino;
  block_t bidx;
  int i;

  SegmentEntry &sentry = GetSegmentManager().GetSegmentEntry(segno);
  if (!TestValidBitmap(blkoff, sentry.cur_valid_map.get())) {
    return;
  }

  // Get the previous summary
  for (i = static_cast<int>(CursegType::kCursegWarmData);
       i <= static_cast<int>(CursegType::kCursegColdData); ++i) {
    CursegInfo *curseg = segment_manager_->CURSEG_I(static_cast<CursegType>(i));
    if (curseg->segno == segno) {
      sum = curseg->sum_blk->entries[blkoff];
      break;
    }
  }
  if (i > static_cast<int>(CursegType::kCursegColdData)) {
    LockedPage sum_page;
    GetSegmentManager().GetSumPage(segno, &sum_page);
    SummaryBlock *sum_node;
    sum_node = sum_page->GetAddress<SummaryBlock>();
    sum = sum_node->entries[blkoff];
  }

  fbl::RefPtr<VnodeF2fs> vnode_refptr;
  // Get the node page
  {
    LockedPage node_page;
    if (zx_status_t err = GetNodeManager().GetNodePage(LeToCpu(sum.nid), &node_page);
        err != ZX_OK) {
      FX_LOGS(ERROR) << "F2fs::CheckIndexInPrevNodes, GetNodePage Error!!!";
      return;
    }
    ino = node_page.GetPage<NodePage>().InoOfNode();
    ZX_ASSERT(VnodeF2fs::Vget(this, ino, &vnode_refptr) == ZX_OK);
    bidx = node_page.GetPage<NodePage>().StartBidxOfNode(*vnode_refptr) + LeToCpu(sum.ofs_in_node);
  }

  // Deallocate previous index in the node page
  vnode_refptr->TruncateHole(bidx, bidx + 1);
}

void F2fs::DoRecoverData(VnodeF2fs &vnode, NodePage &page) {
  uint32_t start, end;
  Summary sum;
  NodeInfoDeprecated ni;

  if (vnode.RecoverInlineData(page) == ZX_OK) {
    // Restored from inline data.
    return;
  }

  start = page.StartBidxOfNode(vnode);
  if (IsInode(page)) {
    end = start + vnode.GetAddrsPerInode();
  } else {
    end = start + kAddrsPerBlock;
  }

  LockedPage dnode_page;
  if (GetNodeManager().GetLockedDnodePage(vnode, start, &dnode_page) != ZX_OK) {
    return;
  }

  dnode_page->WaitOnWriteback();

  GetNodeManager().GetNodeInfo(dnode_page.GetPage<NodePage>().NidOfNode(), ni);
  ZX_DEBUG_ASSERT(ni.ino == page.InoOfNode());
  ZX_DEBUG_ASSERT(dnode_page.GetPage<NodePage>().OfsOfNode() == page.OfsOfNode());

  zx::result<uint32_t> result;
  if (result = GetNodeManager().GetOfsInDnode(vnode, start); result.is_error()) {
    return;
  }
  uint32_t offset_in_dnode = result.value();

  for (; start < end; ++start) {
    block_t src, dest;

    src = DatablockAddr(&dnode_page.GetPage<NodePage>(), offset_in_dnode);
    dest = DatablockAddr(&page, offset_in_dnode);

    if (src != dest && dest != kNewAddr && dest != kNullAddr) {
      if (src == kNullAddr) {
        ZX_ASSERT(vnode.ReserveNewBlock(dnode_page.GetPage<NodePage>(), offset_in_dnode) == ZX_OK);
      }

      // Check the previous node page having this index
      CheckIndexInPrevNodes(dest);

      GetSegmentManager().SetSummary(&sum, dnode_page.GetPage<NodePage>().NidOfNode(),
                                     offset_in_dnode, ni.version);

      // Write dummy data page
      GetSegmentManager().RecoverDataPage(sum, src, dest);
      vnode.SetDataBlkaddr(dnode_page.GetPage<NodePage>(), offset_in_dnode, dest);
      vnode.UpdateExtentCache(dest, page.StartBidxOfNode(vnode));
    }
    ++offset_in_dnode;
  }

  // Write node page in place
  GetSegmentManager().SetSummary(&sum, dnode_page.GetPage<NodePage>().NidOfNode(), 0, 0);
  if (IsInode(*dnode_page)) {
    vnode.MarkInodeDirty();
  }

  dnode_page.GetPage<NodePage>().CopyNodeFooterFrom(page);
  dnode_page.GetPage<NodePage>().FillNodeFooter(ni.nid, ni.ino, page.OfsOfNode(), false);
  dnode_page->SetDirty();
}

void F2fs::RecoverData(FsyncInodeList &inode_list, CursegType type) {
  SuperblockInfo &superblock_info = GetSuperblockInfo();
  uint64_t cp_ver = LeToCpu(superblock_info.GetCheckpoint().checkpoint_ver);
  block_t blkaddr = segment_manager_->NextFreeBlkAddr(type);

  while (true) {
    LockedPage page;
    // Eliminate duplicate node block reads using a meta inode cache.
    if (zx_status_t ret = GetMetaVnode().GrabCachePage(blkaddr, &page); ret != ZX_OK) {
      return;
    }

    auto page_or = MakeReadOperation(std::move(page), blkaddr, PageType::kNode);
    if (page_or.is_error()) {
      break;
    }

    page = std::move(*page_or);
    if (cp_ver != page.GetPage<NodePage>().CpverOfNode()) {
      break;
    }

    if (auto entry = GetFsyncInode(inode_list, page.GetPage<NodePage>().InoOfNode());
        entry != nullptr) {
      DoRecoverData(entry->GetVnode(), page.GetPage<NodePage>());
      if (entry->GetLastDnodeBlkaddr() == blkaddr) {
        inode_list.erase(*entry);
      }
    }
    // check next segment
    blkaddr = page.GetPage<NodePage>().NextBlkaddrOfNode();
    page->ClearUptodate();
  }

  GetSegmentManager().AllocateNewSegments();
}

void F2fs::RecoverFsyncData() {
  SuperblockInfo &superblock_info = GetSuperblockInfo();
  FsyncInodeList inode_list;

  // Step #1: find fsynced inode numbers
  if (auto result = FindFsyncDnodes(inode_list); result == ZX_OK) {
    // Step #2: recover data
    if (!inode_list.is_empty()) {
      superblock_info.SetOnRecovery();
      RecoverData(inode_list, CursegType::kCursegWarmNode);
      superblock_info.ClearOnRecovery();
      ZX_DEBUG_ASSERT(inode_list.is_empty());
      GetMetaVnode().InvalidatePages(GetSegmentManager().GetMainAreaStartBlock());
      WriteCheckpoint(false, false);
    }
  }
  // TODO: Handle error cases
  GetMetaVnode().InvalidatePages(GetSegmentManager().GetMainAreaStartBlock());
  DestroyFsyncDnodes(inode_list);
}

}  // namespace f2fs
