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

zx_status_t F2fs::RecoverDentry(Page *ipage, VnodeF2fs *vnode) {
  Node *raw_node = static_cast<Node *>(PageAddress(ipage));
  Inode *raw_inode = &(raw_node->i);
  fbl::RefPtr<VnodeF2fs> dir_refptr;
  Dir *dir;
  DirEntry *de;
  Page *page = nullptr;
  zx_status_t err = ZX_OK;

  if (!GetNodeManager().IsDentDnode(*ipage))
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

  de = dir->FindEntry(vnode->GetNameView(), &page);
  if (de) {
#if 0  // porting needed
    // kunmap(page);
#endif
    F2fsPutPage(page, 0);
  } else {
    dir->AddLink(vnode->GetNameView(), vnode);
  }
  Iput(dir);
out:
#if 0  // porting needed
  // kunmap(ipage);
#endif
  return err;
}

zx_status_t F2fs::RecoverInode(VnodeF2fs *vnode, Page *node_page) {
  void *kaddr = PageAddress(node_page);
  struct Node *raw_node = static_cast<Node *>(kaddr);
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

  // create a page for a read buffer
  Page *page = GrabCachePage(nullptr, superblock_info_->GetNodeIno(), 0);
  if (!page)
    return ZX_ERR_NO_MEMORY;
#if 0  // porting needed
  // lock_page(page);
#endif

  while (true) {
    if (VnodeF2fs::Readpage(this, page, blkaddr, kReadSync)) {
      break;
    }

    if (cp_ver != NodeManager::CpverOfNode(*page)) {
      break;
    }

    if (!NodeManager::IsFsyncDnode(*page)) {
      /* check next segment */
      blkaddr = NodeManager::NextBlkaddrOfNode(*page);
      ClearPageUptodate(page);
      continue;
    }

    FsyncInodeEntry *entry = GetFsyncInode(head, NodeManager::InoOfNode(*page));
    if (entry) {
      entry->blkaddr = blkaddr;
      if (IsInode(page) && NodeManager::IsDentDnode(*page)) {
        entry->vnode->SetFlag(InodeInfoFlag::kIncLink);
      }
    } else {
      if (IsInode(page) && NodeManager::IsDentDnode(*page)) {
        if (GetNodeManager().RecoverInodePage(*page)) {
          err = ZX_ERR_NO_MEMORY;
          break;
        }
      }

      // TODO: Without cache, Vget cannot retrieve the node page for the fsyncd file
      // that was created after the last checkpoint before spo. (i.e., IsDentDnode)
      // It expects RecoverInodePage() creates a cached page for the inode.
      if (err = VnodeF2fs::Vget(this, NodeManager::InoOfNode(*page), &vnode_refptr); err != ZX_OK) {
        break;
      }

      // add this fsync inode to the list */
      // entry = kmem_cache_alloc(fsync_entry_slab, GFP_NOFS);
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

    if (IsInode(page)) {
      err = RecoverInode(entry->vnode.get(), page);
      if (err) {
        break;
      }
    }

    // get the next block informatio from
    blkaddr = NodeManager::NextBlkaddrOfNode(*page);
    ClearPageUptodate(page);
  }

#if 0  // porting needed
out:
  // unlock_page(page);
  //__free_pages(page, 0);
#endif
  delete page;
  return err;
}

void F2fs::DestroyFsyncDnodes(list_node_t *head) {
  list_node_t *this_node;
  FsyncInodeEntry *entry;
  list_for_every(head, this_node) {
    entry = containerof(this_node, FsyncInodeEntry, list);
    Iput(entry->vnode.get());
    entry->vnode.reset();
    list_delete(&entry->list);
#if 0  // porting needed
    // kmem_cache_free(fsync_entry_slab, entry);
#endif
    delete entry;
  }
}

void F2fs::CheckIndexInPrevNodes(block_t blkaddr) {
  SuperblockInfo &superblock_info = GetSuperblockInfo();
  SegmentEntry *sentry;
  uint32_t segno = segment_manager_->GetSegNo(blkaddr);
  uint16_t blkoff = static_cast<uint16_t>(segment_manager_->GetSegOffFromSeg0(blkaddr) &
                                          (superblock_info.GetBlocksPerSeg() - 1));
  Summary sum;
  nid_t ino;
  void *kaddr;
  fbl::RefPtr<VnodeF2fs> vnode_refptr;
  VnodeF2fs *vnode;
  Page *node_page = nullptr;
  block_t bidx;
  int i;
  __UNUSED zx_status_t err = 0;

  sentry = GetSegmentManager().GetSegmentEntry(segno);
  if (!TestValidBitmap(blkoff, sentry->cur_valid_map.get()))
    return;

  /* Get the previous summary */
  for (i = static_cast<int>(CursegType::kCursegWarmData);
       i <= static_cast<int>(CursegType::kCursegColdData); i++) {
    CursegInfo *curseg = segment_manager_->CURSEG_I(static_cast<CursegType>(i));
    if (curseg->segno == segno) {
      sum = curseg->sum_blk->entries[blkoff];
      break;
    }
  }
  if (i > static_cast<int>(CursegType::kCursegColdData)) {
    Page *sum_page = GetSegmentManager().GetSumPage(segno);
    SummaryBlock *sum_node;
    kaddr = PageAddress(sum_page);
    sum_node = static_cast<SummaryBlock *>(kaddr);
    sum = sum_node->entries[blkoff];
    F2fsPutPage(sum_page, 1);
  }

  /* Get the node page */
  err = GetNodeManager().GetNodePage(LeToCpu(sum.nid), &node_page);
#ifdef F2FS_BU_DEBUG
  if (err) {
    FX_LOGS(ERROR) << "F2fs::CheckIndexInPrevNodes, GetNodePage Error!!!";
    return;
  }
#endif
  bidx = NodeManager::StartBidxOfNode(*node_page) + LeToCpu(sum.ofs_in_node);
  ino = NodeManager::InoOfNode(*node_page);
  F2fsPutPage(node_page, 1);

  /* Deallocate previous index in the node page */
#if 0  // porting needed
  // vnode = F2fsIgetNowait(ino);
#else
  VnodeF2fs::Vget(this, ino, &vnode_refptr);
  vnode = vnode_refptr.get();
#endif
  vnode->TruncateHole(bidx, bidx + 1);
  Iput(vnode);
}

void F2fs::DoRecoverData(VnodeF2fs *vnode, Page *page, block_t blkaddr) {
  uint32_t start, end;
  DnodeOfData dn;
  Summary sum;
  NodeInfo ni;

  start = NodeManager::StartBidxOfNode(*page);
  if (IsInode(page)) {
    end = start + kAddrsPerInode;
  } else {
    end = start + kAddrsPerBlock;
  }

  NodeManager::SetNewDnode(dn, vnode, nullptr, nullptr, 0);
  if (GetNodeManager().GetDnodeOfData(dn, start, 0))
    return;

  WaitOnPageWriteback(dn.node_page);

  GetNodeManager().GetNodeInfo(dn.nid, ni);
  ZX_ASSERT(ni.ino == NodeManager::InoOfNode(*page));
  ZX_ASSERT(NodeManager::OfsOfNode(*dn.node_page) == NodeManager::OfsOfNode(*page));

  for (; start < end; start++) {
    block_t src, dest;

    src = DatablockAddr(dn.node_page, dn.ofs_in_node);
    dest = DatablockAddr(page, dn.ofs_in_node);

    if (src != dest && dest != kNewAddr && dest != kNullAddr) {
      if (src == kNullAddr) {
        zx_status_t err = vnode->ReserveNewBlock(&dn);
        ZX_ASSERT(err == ZX_OK);
      }

      /* Check the previous node page having this index */
      CheckIndexInPrevNodes(dest);

      GetSegmentManager().SetSummary(&sum, dn.nid, dn.ofs_in_node, ni.version);

      /* write dummy data page */
      GetSegmentManager().RecoverDataPage(nullptr, &sum, src, dest);
      vnode->UpdateExtentCache(dest, &dn);
    }
    dn.ofs_in_node++;
  }

  /* write node page in place */
  GetSegmentManager().SetSummary(&sum, dn.nid, 0, 0);
  if (IsInode(dn.node_page))
    GetNodeManager().SyncInodePage(dn);

  NodeManager::CopyNodeFooter(*dn.node_page, *page);
  NodeManager::FillNodeFooter(*dn.node_page, dn.nid, ni.ino, NodeManager::OfsOfNode(*page), false);
#if 0  // porting needed
  // set_page_dirty(dn.node_page, this);
#else
  FlushDirtyNodePage(this, dn.node_page);
#endif

  GetNodeManager().RecoverNodePage(*dn.node_page, sum, ni, blkaddr);
  F2fsPutDnode(&dn);
}

void F2fs::RecoverData(list_node_t *head, CursegType type) {
  SuperblockInfo &superblock_info = GetSuperblockInfo();
  uint64_t cp_ver = LeToCpu(superblock_info.GetCheckpoint().checkpoint_ver);
  Page *page = nullptr;
  block_t blkaddr;

  blkaddr = segment_manager_->NextFreeBlkAddr(type);

  /* read node page */
  page = GrabCachePage(nullptr, superblock_info_->GetNodeIno(), 0);
  if (page == nullptr)
    return;
#if 0  // porting needed
  // lock_page(page);
#endif

  while (true) {
    FsyncInodeEntry *entry;

    if (VnodeF2fs::Readpage(this, page, blkaddr, kReadSync))
      goto out;

    if (cp_ver != NodeManager::CpverOfNode(*page))
      goto out;

    entry = GetFsyncInode(head, NodeManager::InoOfNode(*page));
    if (!entry)
      goto next;

    DoRecoverData(entry->vnode.get(), page, blkaddr);

    if (entry->blkaddr == blkaddr) {
      list_delete(&entry->list);
      Iput(entry->vnode.get());
      entry->vnode.reset();

#if 0  // porting needed
      // kmem_cache_free(fsync_entry_slab, entry);
#endif
      delete entry;
    }
  next:
    /* check next segment */
    blkaddr = NodeManager::NextBlkaddrOfNode(*page);
    ClearPageUptodate(page);
  }
out:
#if 0  // porting needed
  // unlock_page(page);
  //__free_pages(page, 0);
#endif
  F2fsPutPage(page, 1);

  GetSegmentManager().AllocateNewSegments();
}

void F2fs::RecoverFsyncData() {
  SuperblockInfo &superblock_info = GetSuperblockInfo();
  list_node_t inode_list;

#if 0  // porting needed
  // fsync_entry_slab = KmemCacheCreate("f2fs_FsyncInodeEntry",
  // 		sizeof(FsyncInodeEntry), nullptr);
  // if (unlikely(!fsync_entry_slab))
  // 	return;
#endif

  list_initialize(&inode_list);

  /* step #1: find fsynced inode numbers */
  if (FindFsyncDnodes(&inode_list)) {
    goto out;
  }

  if (list_is_empty(&inode_list)) {
    goto out;
  }

  /* step #2: recover data */
  superblock_info.SetOnRecovery();
  RecoverData(&inode_list, CursegType::kCursegWarmNode);
  superblock_info.ClearOnRecovery();
  ZX_ASSERT(list_is_empty(&inode_list));
out:
  DestroyFsyncDnodes(&inode_list);
#if 0  // porting needed
  // kmem_cache_destroy(fsync_entry_slab);
#endif
  WriteCheckpoint(false, false);
}

}  // namespace f2fs
