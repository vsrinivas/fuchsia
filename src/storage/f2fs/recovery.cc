// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/f2fs/f2fs.h"

namespace f2fs {

bool F2fs::SpaceForRollForward() {
  SbInfo &sbi = GetSbInfo();
  if (sbi.last_valid_block_count + sbi.alloc_valid_block_count > sbi.user_block_count)
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
  Page *page;
  zx_status_t err = ZX_OK;

  if (!Nodemgr().IsDentDnode(ipage))
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
  SbInfo &sbi = GetSbInfo();
  uint64_t cp_ver = LeToCpu(sbi.ckpt->checkpoint_ver);
  fbl::RefPtr<VnodeF2fs> vnode_refptr;
  zx_status_t err = ZX_OK;

  // retrieve the curseg information of kCursegWarmNode
  CursegInfo *curseg = SegMgr::CURSEG_I(&sbi, CursegType::kCursegWarmNode);
  // get blkaddr from which it starts finding fsyncd dnode block
  block_t blkaddr = StartBlock(&sbi, curseg->segno) + curseg->next_blkoff;

  // create a page for a read buffer
  Page *page = GrabCachePage(nullptr, NodeIno(sbi_.get()), 0);
  if (!page)
    return ZX_ERR_NO_MEMORY;
#if 0  // porting needed
  // lock_page(page);
#endif

  while (true) {
    if (VnodeF2fs::Readpage(this, page, blkaddr, kReadSync)) {
      break;
    }

    if (cp_ver != Nodemgr().CpverOfNode(page)) {
      break;
    }

    if (!Nodemgr().IsFsyncDnode(page)) {
      /* check next segment */
      blkaddr = NodeMgr::NextBlkaddrOfNode(page);
      ClearPageUptodate(page);
      continue;
    }

    FsyncInodeEntry *entry = GetFsyncInode(head, Nodemgr().InoOfNode(page));
    if (entry) {
      entry->blkaddr = blkaddr;
      if (IsInode(page) && Nodemgr().IsDentDnode(page)) {
        entry->vnode->SetFlag(InodeInfoFlag::kIncLink);
      }
    } else {
      if (IsInode(page) && Nodemgr().IsDentDnode(page)) {
        if (Nodemgr().RecoverInodePage(page)) {
          err = ZX_ERR_NO_MEMORY;
          break;
        }
      }

      // TODO: Without cache, Vget cannot retrieve the node page for the fsyncd file
      // that was created after the last checkpoint before spo. (i.e., IsDentDnode)
      // It expects RecoverInodePage() creates a cached page for the inode.
      if (err = VnodeF2fs::Vget(this, Nodemgr().InoOfNode(page), &vnode_refptr); err != ZX_OK) {
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
    blkaddr = NodeMgr::NextBlkaddrOfNode(page);
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
  SbInfo &sbi = GetSbInfo();
  SegEntry *sentry;
  uint32_t segno = GetSegNo(&sbi, blkaddr);
  uint16_t blkoff = GetSegOffFromSeg0(&sbi, blkaddr) & (sbi.blocks_per_seg - 1);
  Summary sum;
  nid_t ino;
  void *kaddr;
  fbl::RefPtr<VnodeF2fs> vnode_refptr;
  VnodeF2fs *vnode;
  Page *node_page = nullptr;
  block_t bidx;
  int i;
  __UNUSED zx_status_t err = 0;

  sentry = Segmgr().GetSegEntry(segno);
  if (!TestValidBitmap(blkoff, reinterpret_cast<char *>(sentry->cur_valid_map)))
    return;

  /* Get the previous summary */
  for (i = static_cast<int>(CursegType::kCursegWarmData);
       i <= static_cast<int>(CursegType::kCursegColdData); i++) {
    CursegInfo *curseg = Segmgr().CURSEG_I(&sbi, static_cast<CursegType>(i));
    if (curseg->segno == segno) {
      sum = curseg->sum_blk->entries[blkoff];
      break;
    }
  }
  if (i > static_cast<int>(CursegType::kCursegColdData)) {
    Page *sum_page = Segmgr().GetSumPage(segno);
    SummaryBlock *sum_node;
    kaddr = PageAddress(sum_page);
    sum_node = static_cast<SummaryBlock *>(kaddr);
    sum = sum_node->entries[blkoff];
    F2fsPutPage(sum_page, 1);
  }

  /* Get the node page */
  err = Nodemgr().GetNodePage(LeToCpu(sum.nid), &node_page);
#ifdef F2FS_BU_DEBUG
  if (err) {
    FX_LOGS(ERROR) << "F2fs::CheckIndexInPrevNodes, GetNodePage Error!!!";
    return;
  }
#endif
  bidx = Nodemgr().StartBidxOfNode(node_page) + LeToCpu(sum.ofs_in_node);
  ino = Nodemgr().InoOfNode(node_page);
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

  start = Nodemgr().StartBidxOfNode(page);
  if (IsInode(page)) {
    end = start + kAddrsPerInode;
  } else {
    end = start + kAddrsPerBlock;
  }

  SetNewDnode(&dn, vnode, nullptr, nullptr, 0);
  if (Nodemgr().GetDnodeOfData(&dn, start, 0))
    return;

  WaitOnPageWriteback(dn.node_page);

  Nodemgr().GetNodeInfo(dn.nid, &ni);
  ZX_ASSERT(ni.ino == Nodemgr().InoOfNode(page));
  ZX_ASSERT(Nodemgr().OfsOfNode(dn.node_page) == Nodemgr().OfsOfNode(page));

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

      Segmgr().SetSummary(&sum, dn.nid, dn.ofs_in_node, ni.version);

      /* write dummy data page */
      Segmgr().RecoverDataPage(nullptr, &sum, src, dest);
      vnode->UpdateExtentCache(dest, &dn);
    }
    dn.ofs_in_node++;
  }

  /* write node page in place */
  Segmgr().SetSummary(&sum, dn.nid, 0, 0);
  if (IsInode(dn.node_page))
    Nodemgr().SyncInodePage(&dn);

  Nodemgr().CopyNodeFooter(dn.node_page, page);
  Nodemgr().FillNodeFooter(dn.node_page, dn.nid, ni.ino, Nodemgr().OfsOfNode(page), false);
#if 0  // porting needed
  // set_page_dirty(dn.node_page, this);
#else
  FlushDirtyNodePage(this, dn.node_page);
#endif

  Nodemgr().RecoverNodePage(dn.node_page, &sum, &ni, blkaddr);
  F2fsPutDnode(&dn);
}

void F2fs::RecoverData(list_node_t *head, CursegType type) {
  SbInfo &sbi = GetSbInfo();
  uint64_t cp_ver = LeToCpu(sbi.ckpt->checkpoint_ver);
  CursegInfo *curseg;
  Page *page;
  block_t blkaddr;

  /* get node pages in the current segment */
  curseg = SegMgr::CURSEG_I(&sbi, type);
  blkaddr = NextFreeBlkAddr(&sbi, curseg);

  /* read node page */
  page = GrabCachePage(nullptr, NodeIno(sbi_.get()), 0);
  if (page == nullptr)
    return;
#if 0  // porting needed
  // lock_page(page);
#endif

  while (true) {
    FsyncInodeEntry *entry;

    if (VnodeF2fs::Readpage(this, page, blkaddr, kReadSync))
      goto out;

    if (cp_ver != Nodemgr().CpverOfNode(page))
      goto out;

    entry = GetFsyncInode(head, Nodemgr().InoOfNode(page));
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
    blkaddr = NodeMgr::NextBlkaddrOfNode(page);
    ClearPageUptodate(page);
  }
out:
#if 0  // porting needed
  // unlock_page(page);
  //__free_pages(page, 0);
#endif
  F2fsPutPage(page, 1);

  Segmgr().AllocateNewSegments();
}

void F2fs::RecoverFsyncData() {
  SbInfo &sbi = GetSbInfo();
  list_node_t inode_list;

#if 0  // porting needed
  // fsync_entry_slab = KmemCacheCreate("f2fs_FsyncInodeEntry",
  // 		sizeof(FsyncInodeEntry), NULL);
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
  sbi.por_doing = 1;
  RecoverData(&inode_list, CursegType::kCursegWarmNode);
  sbi.por_doing = 0;
  ZX_ASSERT(list_is_empty(&inode_list));
out:
  DestroyFsyncDnodes(&inode_list);
#if 0  // porting needed
  // kmem_cache_destroy(fsync_entry_slab);
#endif
  WriteCheckpoint(false, false);
}

}  // namespace f2fs
