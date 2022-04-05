// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/f2fs/f2fs.h"

namespace f2fs {

bool F2fs::SpaceForRollForward() {
  SuperblockInfo &superblock_info = GetSuperblockInfo();
  if (superblock_info.GetLastValidBlockCount() + superblock_info.GetAllocValidBlockCount() >
      superblock_info.GetUserBlockCount())
    return false;
  return true;
}

FsyncInodeEntry *F2fs::GetFsyncInode(list_node_t *head, nid_t ino) {
  list_node_t *this_node;
  FsyncInodeEntry *entry;

  list_for_every(head, this_node) {
    entry = containerof(this_node, FsyncInodeEntry, list);
    if (entry->vnode->Ino() == ino)
      return entry;
  }
  return nullptr;
}

zx_status_t F2fs::RecoverDentry(NodePage *ipage, VnodeF2fs *vnode) {
  Node *raw_node = ipage->GetAddress<Node>();
  Inode *raw_inode = &(raw_node->i);
  fbl::RefPtr<VnodeF2fs> dir_refptr;
  Dir *dir;
  zx_status_t err = ZX_OK;

  if (!ipage->IsDentDnode())
    goto out;

  err = VnodeF2fs::Vget(this, LeToCpu(raw_inode->i_pino), &dir_refptr);
  if (err != ZX_OK) {
    goto out;
  }

  dir = static_cast<Dir *>(dir_refptr.get());

#if 0  // porting needed
  // parent.d_inode = dir;
  // dent.d_parent = &parent;
  // dent.d_name.len = LeToCpu(raw_inode->i_namelen);
  // dent.d_name.name = raw_inode->i_name;
#endif

  if (auto dir_entry = dir->FindEntry(vnode->GetNameView()); dir_entry.is_error()) {
    dir->AddLink(vnode->GetNameView(), vnode);
  }
out:
#if 0  // porting needed
  // kunmap(ipage);
#endif
  return err;
}

zx_status_t F2fs::RecoverInode(VnodeF2fs *vnode, NodePage *node_page) {
  struct Node *raw_node = node_page->GetAddress<Node>();
  struct Inode *raw_inode = &(raw_node->i);

  vnode->SetMode(LeToCpu(raw_inode->i_mode));
  vnode->SetSize(LeToCpu(raw_inode->i_size));
  vnode->SetATime(LeToCpu(raw_inode->i_atime), LeToCpu(raw_inode->i_atime_nsec));
  vnode->SetCTime(LeToCpu(raw_inode->i_ctime), LeToCpu(raw_inode->i_ctime_nsec));
  vnode->SetMTime(LeToCpu(raw_inode->i_mtime), LeToCpu(raw_inode->i_mtime_nsec));

  return RecoverDentry(node_page, vnode);
}

zx_status_t F2fs::FindFsyncDnodes(list_node_t *head) {
  SuperblockInfo &superblock_info = GetSuperblockInfo();
  uint64_t cp_ver = LeToCpu(superblock_info.GetCheckpoint().checkpoint_ver);
  fbl::RefPtr<VnodeF2fs> vnode_refptr;
  zx_status_t err = ZX_OK;

  // retrieve the curseg information of kCursegWarmNode
  CursegInfo *curseg = segment_manager_->CURSEG_I(CursegType::kCursegWarmNode);
  // get blkaddr from which it starts finding fsyncd dnode block
  block_t blkaddr = segment_manager_->StartBlock(curseg->segno) + curseg->next_blkoff;

  // alloc a temporal page to read node blocks.
  fbl::RefPtr<NodePage> page;
  if (zx_status_t ret = GetNodeVnode().GrabCachePage(-1, &page); ret != ZX_OK) {
    return ret;
  }
#if 0  // porting needed
  // lock_page(page);
#endif

  while (true) {
    if (MakeOperation(storage::OperationType::kRead, page, blkaddr, PageType::kNode)) {
      break;
    }

    if (cp_ver != page->CpverOfNode()) {
      break;
    }

    if (!page->IsFsyncDnode()) {
      /* check next segment */
      blkaddr = page->NextBlkaddrOfNode();
      page->ClearUptodate();
      continue;
    }

    FsyncInodeEntry *entry = GetFsyncInode(head, page->InoOfNode());
    if (entry) {
      entry->blkaddr = blkaddr;
      if (IsInode(*page) && page->IsDentDnode()) {
        entry->vnode->SetFlag(InodeInfoFlag::kIncLink);
      }
    } else {
      if (IsInode(*page) && page->IsDentDnode()) {
        if (GetNodeManager().RecoverInodePage(*page)) {
          err = ZX_ERR_NO_MEMORY;
          break;
        }
      }

      // TODO: Without cache, Vget cannot retrieve the node page for the fsyncd file
      // that was created after the last checkpoint before spo. (i.e., IsDentDnode)
      // It expects RecoverInodePage() creates a cached page for the inode.
      if (err = VnodeF2fs::Vget(this, page->InoOfNode(), &vnode_refptr); err != ZX_OK) {
        break;
      }

      // add this fsync inode to the list
      entry = new FsyncInodeEntry;
      if (!entry) {
        err = ZX_ERR_NO_MEMORY;
        vnode_refptr.reset();
        break;
      }

      list_initialize(&entry->list);
      entry->vnode = std::move(vnode_refptr);
      entry->blkaddr = blkaddr;
      list_add_tail(&entry->list, head);
    }

    if (IsInode(*page)) {
      err = RecoverInode(entry->vnode.get(), page.get());
      if (err) {
        break;
      }
    }

    // get the next block informatio from
    blkaddr = page->NextBlkaddrOfNode();
    page->ClearUptodate();
  }

#if 0  // porting needed
out:
  // unlock_page(page);
  //__free_pages(page, 0);
#endif
  return err;
}

void F2fs::DestroyFsyncDnodes(list_node_t *head) {
  list_node_t *this_node;
  FsyncInodeEntry *entry;
  list_for_every(head, this_node) {
    entry = containerof(this_node, FsyncInodeEntry, list);
    entry->vnode.reset();
    list_delete(&entry->list);
    delete entry;
  }
}

void F2fs::CheckIndexInPrevNodes(block_t blkaddr) {
  SuperblockInfo &superblock_info = GetSuperblockInfo();
  uint32_t segno = segment_manager_->GetSegmentNumber(blkaddr);
  uint16_t blkoff = static_cast<uint16_t>(segment_manager_->GetSegOffFromSeg0(blkaddr) &
                                          (superblock_info.GetBlocksPerSeg() - 1));
  Summary sum;
  nid_t ino;
  fbl::RefPtr<VnodeF2fs> vnode_refptr;
  VnodeF2fs *vnode;
  fbl::RefPtr<NodePage> node_page;
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
    fbl::RefPtr<Page> sum_page;
    GetSegmentManager().GetSumPage(segno, &sum_page);
    SummaryBlock *sum_node;
    sum_node = sum_page->GetAddress<SummaryBlock>();
    sum = sum_node->entries[blkoff];
    Page::PutPage(std::move(sum_page), true);
  }

  // Get the node page
  if (zx_status_t err = GetNodeManager().GetNodePage(LeToCpu(sum.nid), &node_page); err != ZX_OK) {
    FX_LOGS(ERROR) << "F2fs::CheckIndexInPrevNodes, GetNodePage Error!!!";
    return;
  }
  bidx = node_page->StartBidxOfNode() + LeToCpu(sum.ofs_in_node);
  ino = node_page->InoOfNode();
  Page::PutPage(std::move(node_page), true);

  // Deallocate previous index in the node page
  VnodeF2fs::Vget(this, ino, &vnode_refptr);
  vnode = vnode_refptr.get();
  vnode->TruncateHole(bidx, bidx + 1);
}

void F2fs::DoRecoverData(VnodeF2fs *vnode, NodePage *page, block_t blkaddr) {
  uint32_t start, end;
  DnodeOfData dn;
  Summary sum;
  NodeInfo ni;

  start = page->StartBidxOfNode();
  if (IsInode(*page)) {
    end = start + kAddrsPerInode;
  } else {
    end = start + kAddrsPerBlock;
  }

  NodeManager::SetNewDnode(dn, vnode, nullptr, nullptr, 0);
  if (GetNodeManager().GetDnodeOfData(dn, start, 0))
    return;

  dn.node_page->WaitOnWriteback();

  GetNodeManager().GetNodeInfo(dn.nid, ni);
  ZX_ASSERT(ni.ino == page->InoOfNode());
  ZX_ASSERT(dn.node_page->OfsOfNode() == page->OfsOfNode());

  for (; start < end; ++start) {
    block_t src, dest;

    src = DatablockAddr(dn.node_page.get(), dn.ofs_in_node);
    dest = DatablockAddr(page, dn.ofs_in_node);

    if (src != dest && dest != kNewAddr && dest != kNullAddr) {
      if (src == kNullAddr) {
        zx_status_t err = vnode->ReserveNewBlock(&dn);
        ZX_ASSERT(err == ZX_OK);
      }

      // Check the previous node page having this index
      CheckIndexInPrevNodes(dest);

      GetSegmentManager().SetSummary(&sum, dn.nid, dn.ofs_in_node, ni.version);

      // write dummy data page
      GetSegmentManager().RecoverDataPage(nullptr, &sum, src, dest);
      vnode->UpdateExtentCache(dest, &dn);
    }
    ++dn.ofs_in_node;
  }

  // write node page in place
  GetSegmentManager().SetSummary(&sum, dn.nid, 0, 0);
  if (IsInode(*(dn.node_page))) {
    dn.vnode->MarkInodeDirty();
  }

  dn.node_page->CopyNodeFooterFrom(*page);
  dn.node_page->FillNodeFooter(dn.nid, ni.ino, page->OfsOfNode(), false);
  dn.node_page->SetDirty();

  GetNodeManager().RecoverNodePage(dn.node_page, sum, ni, blkaddr);
  F2fsPutDnode(&dn);
}

void F2fs::RecoverData(list_node_t *head, CursegType type) {
  SuperblockInfo &superblock_info = GetSuperblockInfo();
  uint64_t cp_ver = LeToCpu(superblock_info.GetCheckpoint().checkpoint_ver);
  block_t blkaddr;

  blkaddr = segment_manager_->NextFreeBlkAddr(type);

  // Alloc a tempotal page to read a chain of node blocks
  // TODO: need to request read IOs w/ uncached pages
  fbl::RefPtr<NodePage> page;
  if (zx_status_t ret = GetNodeVnode().GrabCachePage(-1, &page); ret != ZX_OK) {
    return;
  }
  while (true) {
    if (MakeOperation(storage::OperationType::kRead, page, blkaddr, PageType::kNode)) {
      break;
    }

    if (cp_ver != page->CpverOfNode()) {
      break;
    }

    if (FsyncInodeEntry *entry = GetFsyncInode(head, page->InoOfNode()); entry != nullptr) {
      DoRecoverData(entry->vnode.get(), page.get(), blkaddr);
      if (entry->blkaddr == blkaddr) {
        list_delete(&entry->list);
        entry->vnode.reset();
        delete entry;
      }
    }
    // check next segment
    blkaddr = page->NextBlkaddrOfNode();
    page->ClearUptodate();
  }
#if 0  // porting needed
  //__free_pages(page, 0);
#endif
  Page::PutPage(std::move(page), true);

  GetSegmentManager().AllocateNewSegments();
}

void F2fs::RecoverFsyncData() {
  SuperblockInfo &superblock_info = GetSuperblockInfo();
  list_node_t inode_list;

  list_initialize(&inode_list);

  // step #1: find fsynced inode numbers
  if (FindFsyncDnodes(&inode_list)) {
    goto out;
  }

  if (list_is_empty(&inode_list)) {
    goto out;
  }

  // step #2: recover data
  superblock_info.SetOnRecovery();
  RecoverData(&inode_list, CursegType::kCursegWarmNode);
  superblock_info.ClearOnRecovery();
  ZX_ASSERT(list_is_empty(&inode_list));
out:
  DestroyFsyncDnodes(&inode_list);
  WriteCheckpoint(false, false);
}

}  // namespace f2fs
