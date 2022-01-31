// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/f2fs/f2fs.h"

namespace f2fs {

// Lock ordering for the change of data block address:
// ->data_page
//  ->node_page
//    update block addresses in the node page
void VnodeF2fs::SetDataBlkaddr(DnodeOfData *dn, block_t new_addr) {
  Page *node_page = dn->node_page.get();
  uint32_t ofs_in_node = dn->ofs_in_node;

  node_page->WaitOnWriteback();

  Node *rn = static_cast<Node *>(node_page->GetAddress());

  // Get physical address of data block
  uint32_t *addr_array = BlkaddrInNode(*rn);
  addr_array[ofs_in_node] = CpuToLe(new_addr);
  node_page->SetDirty();
}

zx_status_t VnodeF2fs::ReserveNewBlock(DnodeOfData *dn) {
  if (dn->vnode->TestFlag(InodeInfoFlag::kNoAlloc))
    return ZX_ERR_ACCESS_DENIED;
  if (zx_status_t ret = Vfs()->IncValidBlockCount(dn->vnode, 1); ret != ZX_OK)
    return ret;

  SetDataBlkaddr(dn, kNewAddr);
  dn->data_blkaddr = kNewAddr;
  Vfs()->GetNodeManager().SyncInodePage(*dn);
  return ZX_OK;
}

#if 0  // porting needed
// int VnodeF2fs::CheckExtentCache(inode *inode, pgoff_t pgofs,
//           buffer_head *bh_result)
// {
//   Inode_info *fi = F2FS_I(inode);
//   SuperblockInfo *superblock_info = F2FS_SB(inode->i_sb);
//   pgoff_t start_fofs, end_fofs;
//   block_t start_blkaddr;

//   ReadLock(&fi->ext.ext_lock);
//   if (fi->ext.len == 0) {
//     ReadUnlock(&fi->ext.ext_lock);
//     return 0;
//   }

//   ++superblock_info->total_hit_ext;
//   start_fofs = fi->ext.fofs;
//   end_fofs = fi->ext.fofs + fi->ext.len - 1;
//   start_blkaddr = fi->ext.blk_addr;

//   if (pgofs >= start_fofs && pgofs <= end_fofs) {
//     uint32_t blkbits = inode->i_sb->s_blocksize_bits;
//     size_t count;

//     clear_buffer_new(bh_result);
//     map_bh(bh_result, inode->i_sb,
//        start_blkaddr + pgofs - start_fofs);
//     count = end_fofs - pgofs + 1;
//     if (count < (UINT_MAX >> blkbits))
//       bh_result->b_size = (count << blkbits);
//     else
//       bh_result->b_size = UINT_MAX;

//     ++superblock_info->read_hit_ext;
//     ReadUnlock(&fi->ext.ext_lock);
//     return 1;
//   }
//   ReadUnlock(&fi->ext.ext_lock);
//   return 0;
// }
#endif

void VnodeF2fs::UpdateExtentCache(block_t blk_addr, DnodeOfData *dn) {
  InodeInfo *fi = &dn->vnode->fi_;
  pgoff_t fofs, start_fofs, end_fofs;
  block_t start_blkaddr, end_blkaddr;

  ZX_ASSERT(blk_addr != kNewAddr);
  fofs = Vfs()->GetNodeManager().StartBidxOfNode(*dn->node_page) + dn->ofs_in_node;

  /* Update the page address in the parent node */
  SetDataBlkaddr(dn, blk_addr);

  do {
    std::lock_guard ext_lock(fi->ext.ext_lock);

    start_fofs = fi->ext.fofs;
    end_fofs = fi->ext.fofs + fi->ext.len - 1;
    start_blkaddr = fi->ext.blk_addr;
    end_blkaddr = fi->ext.blk_addr + fi->ext.len - 1;

    /* Drop and initialize the matched extent */
    if (fi->ext.len == 1 && fofs == start_fofs)
      fi->ext.len = 0;

    /* Initial extent */
    if (fi->ext.len == 0) {
      if (blk_addr != kNullAddr) {
        fi->ext.fofs = fofs;
        fi->ext.blk_addr = blk_addr;
        fi->ext.len = 1;
      }
      break;
    }

    /* Frone merge */
    if (fofs == start_fofs - 1 && blk_addr == start_blkaddr - 1) {
      --fi->ext.fofs;
      --fi->ext.blk_addr;
      ++fi->ext.len;
      break;
    }

    /* Back merge */
    if (fofs == end_fofs + 1 && blk_addr == end_blkaddr + 1) {
      ++fi->ext.len;
      break;
    }

    /* Split the existing extent */
    if (fi->ext.len > 1 && fofs >= start_fofs && fofs <= end_fofs) {
      if ((end_fofs - fofs) < (fi->ext.len >> 1)) {
        fi->ext.len = static_cast<uint32_t>(fofs - start_fofs);
      } else {
        fi->ext.fofs = fofs + 1;
        fi->ext.blk_addr = static_cast<uint32_t>(start_blkaddr + fofs - start_fofs + 1);
        fi->ext.len -= fofs - start_fofs + 1;
      }
      break;
    }
    return;
  } while (false);

  Vfs()->GetNodeManager().SyncInodePage(*dn);
}

zx_status_t VnodeF2fs::FindDataPage(pgoff_t index, fbl::RefPtr<Page> *out) {
  DnodeOfData dn;

  if (zx_status_t ret = FindPage(index, out); ret == ZX_OK) {
    if ((*out)->IsUptodate()) {
      return ret;
    }
    Page::PutPage(std::move(*out), false);
  }

  NodeManager::SetNewDnode(dn, this, nullptr, nullptr, 0);
  if (zx_status_t err = Vfs()->GetNodeManager().GetDnodeOfData(dn, index, kRdOnlyNode);
      err != ZX_OK)
    return err;
  F2fsPutDnode(&dn);

  if (dn.data_blkaddr == kNullAddr)
    return ZX_ERR_NOT_FOUND;

  // By fallocate(), there is no cached page, but with kNewAddr
  if (dn.data_blkaddr == kNewAddr)
    return ZX_ERR_INVALID_ARGS;

  if (zx_status_t err = GrabCachePage(index, out); err != ZX_OK) {
    return err;
  }

  if (zx_status_t err = Readpage(Vfs(), (*out).get(), dn.data_blkaddr, kReadSync); err != ZX_OK) {
    Page::PutPage(std::move(*out), true);
    return err;
  }

  (*out)->Unlock();
  return ZX_OK;
}

/**
 * If it tries to access a hole, return an error.
 * Because, the callers, functions in dir.c and GC, should be able to know
 * whether this page exists or not.
 */
zx_status_t VnodeF2fs::GetLockDataPage(pgoff_t index, fbl::RefPtr<Page> *out) {
  DnodeOfData dn;
  NodeManager::SetNewDnode(dn, this, nullptr, nullptr, 0);
  if (zx_status_t err = Vfs()->GetNodeManager().GetDnodeOfData(dn, index, kRdOnlyNode);
      err != ZX_OK)
    return err;
  F2fsPutDnode(&dn);

  if (dn.data_blkaddr == kNullAddr) {
    return ZX_ERR_NOT_FOUND;
  }

  if (zx_status_t ret = GrabCachePage(index, out); ret != ZX_OK) {
    return ret;
  }

  if ((*out)->IsUptodate()) {
    return ZX_OK;
  }

  ZX_ASSERT(dn.data_blkaddr != kNewAddr);
  ZX_ASSERT(dn.data_blkaddr != kNullAddr);

  if (zx_status_t err = VnodeF2fs::Readpage(Vfs(), (*out).get(), dn.data_blkaddr, kReadSync);
      err != ZX_OK) {
    Page::PutPage(std::move(*out), true);
    return err;
  }
  return ZX_OK;
}

// Caller ensures that this data page is never allocated.
// A new zero-filled data page is allocated in the page cache.
zx_status_t VnodeF2fs::GetNewDataPage(pgoff_t index, bool new_i_size, fbl::RefPtr<Page> *out) {
  DnodeOfData dn;
  NodeManager::SetNewDnode(dn, this, nullptr, nullptr, 0);
  if (zx_status_t err = Vfs()->GetNodeManager().GetDnodeOfData(dn, index, 0); err != ZX_OK) {
    return err;
  }

  if (dn.data_blkaddr == kNullAddr) {
    if (zx_status_t ret = ReserveNewBlock(&dn); ret != ZX_OK) {
      F2fsPutDnode(&dn);
      return ret;
    }
  }
  F2fsPutDnode(&dn);

  if (zx_status_t ret = GrabCachePage(index, out); ret != ZX_OK) {
    return ret;
  }

  if ((*out)->IsUptodate()) {
    return ZX_OK;
  }

  if (dn.data_blkaddr == kNewAddr) {
    (*out)->ZeroUserSegment(0, kPageSize);
  } else {
    if (zx_status_t err = Readpage(Vfs(), (*out).get(), dn.data_blkaddr, kReadSync); err != ZX_OK) {
      Page::PutPage(std::move(*out), true);
      return err;
    }
  }
  (*out)->SetUptodate();

  // if (new_i_size &&
  //   i_size_read(inode) < ((index + 1) << kPageCacheShift)) {
  //   i_size_write(inode, ((index + 1) << kPageCacheShift));
  //   mark_inode_dirty_sync(inode);
  // }
  if (new_i_size && GetSize() < ((index + 1) << kPageCacheShift)) {
    SetSize((index + 1) << kPageCacheShift);
#if 0  // porting needed
    // mark_inode_dirty_sync(inode);
#endif
  }

  return ZX_OK;
}

#if 0  // porting needed
// static void read_end_io(bio *bio, int err)
// {
//   const int uptodate = TestBit(BIO_UPTODATE, &bio->bi_flags);
//   bio_vec *bvec = bio->bi_io_vec + bio->bi_vcnt - 1;

//   do {
//     page *page = bvec->bv_page;

//     if (--bvec >= bio->bi_io_vec)
//       prefetchw(&bvec->bv_page->flags);

//     if (uptodate) {
//       SetPageUptodate(page);
//     } else {
//       ClearPageUptodate(page);
//       SetPageError(page);
//     }
//     unlock_page(page);
//   } while (bvec >= bio->bi_io_vec);
//   kfree(bio->bi_private);
//   bio_put(bio);
// }
#endif

/**
 * Fill the locked page with data located in the block address.
 * Read operation is synchronous, and caller must unlock the page.
 */
zx_status_t VnodeF2fs::Readpage(F2fs *fs, Page *page, block_t blk_addr, int type) {
#if 0  // porting needed
  //   block_device *bdev = superblock_info->sb->s_bdev;
  //   bool sync = (type == kReadSync);
  //   bio *bio;

  //   /* This page can be already read by other threads */
  //   if (PageUptodate(page)) {
  //     if (!sync)
  //       unlock_page(page);
  //     return 0;
  //   }

  //   down_read(&superblock_info->bio_sem);

  //   /* Allocate a new bio */
  //   bio = f2fs_bio_alloc(bdev, blk_addr << (superblock_info->GetLogBlocksize() - 9),
  //         1, GFP_NOFS | __GFP_HIGH);

  //   /* Initialize the bio */
  //   bio->bi_end_io = read_end_io;
  //   if (bio_add_page(bio, page, kPageSize, 0) < kPageSize) {
  //     kfree(bio->bi_private);
  //     bio_put(bio);
  //     up_read(&superblock_info->bio_sem);
  //     return -EFAULT;
  //   }

  //   submit_bio(type, bio);
  //   up_read(&superblock_info->bio_sem);

  //   /* wait for read completion if sync */
  //   if (sync) {
  //     lock_page(page);
  //     if (PageError(page))
  //       return -EIO;
  //   }
  // return 0;
#else
  if (page->IsUptodate()) {
    return ZX_OK;
  }
  if (zx_status_t ret = fs->GetBc().Readblk(blk_addr, page->GetAddress()); ret != ZX_OK) {
    return ret;
  }
  // TODO:Move it to EndIO when async io is available
  page->SetUptodate();
  return ZX_OK;
#endif
}

/**
 * This function should be used by the data read flow only where it
 * does not check the "create" flag that indicates block allocation.
 * The reason for this special functionality is to exploit VFS readahead
 * mechanism.
 */
#if 0  // porting needed
// int VnodeF2fs::GetDataBlockRo(inode *inode, sector_t iblock,
//       buffer_head *bh_result, int create)
// {
//   uint32_t blkbits = inode->i_sb->s_blocksize_bits;
//   unsigned maxblocks = bh_result->b_size >> blkbits;
//   DnodeOfData dn;
//   pgoff_t pgofs;
//   //int err = 0;

//   /* Get the page offset from the block offset(iblock) */
//   pgofs =  (pgoff_t)(iblock >> (kPageCacheShift - blkbits));

//   if (VnodeF2fs::CheckExtentCache(inode, pgofs, bh_result))
//     return 0;

//   /* When reading holes, we need its node page */
//   //TODO(unknown): inode should be replaced with vnodef2fs
//   //SetNewDnode(&dn, inode, nullptr, nullptr, 0);
//   // TODO(unknown): shoud be replaced with NodeManager->GetDnodeOfData
//   /*err = get_DnodeOfData(&dn, pgofs, kRdOnlyNode);
//   if (err)
//     return (err == ZX_ERR_NOT_FOUND) ? 0 : err; */

//   /* It does not support data allocation */
//   ZX_ASSERT(!create);

//   if (dn.data_blkaddr != kNewAddr && dn.data_blkaddr != kNullAddr) {
//     uint32_t end_offset;

//     end_offset = IsInode(dn.node_page) ?
//         kAddrsPerInode :
//         kAddrsPerBlock;

//     clear_buffer_new(bh_result);

//     /* Give more consecutive addresses for the read ahead */
//     for (uint32_t i = 0; i < end_offset - dn.ofs_in_node; ++i)
//       if (((DatablockAddr(dn.node_page,
//               dn.ofs_in_node + i))
//         != (dn.data_blkaddr + i)) || maxblocks == i)
//         break;
//     //map_bh(bh_result, inode->i_sb, dn.data_blkaddr);
//     bh_result->b_size = (i << blkbits);
//   }
//   F2fsPutDnode(&dn);
//   return 0;
// }

// int VnodeF2fs::F2fsReadDataPage(file *file, page *page)
// {
//   return mpage_readpage(page, VnodeF2fs::GetDataBlockRo);
// }

// int VnodeF2fs::F2fsReadDataPages(file *file,
//       address_space *mapping,
//       list_node_t *pages, unsigned nr_pages)
// {
//   return mpage_readpages(mapping, pages, nr_pages, VnodeF2fs::GetDataBlockRo);
// }
#endif

zx_status_t VnodeF2fs::DoWriteDataPage(Page *page) {
  DnodeOfData dn;
  NodeManager::SetNewDnode(dn, this, nullptr, nullptr, 0);
  if (zx_status_t err = Vfs()->GetNodeManager().GetDnodeOfData(dn, page->GetIndex(), kRdOnlyNode);
      err != ZX_OK) {
    return err;
  }

  block_t old_blk_addr = dn.data_blkaddr;
  // This page is already truncated
  if (old_blk_addr == kNullAddr) {
    F2fsPutDnode(&dn);
    return ZX_OK;
  }

  page->SetWriteback();

  // If current allocation needs SSR,
  // it had better in-place writes for updated data.
  // TODO: Impl IsCodeData
  if (old_blk_addr != kNewAddr && /*! NodeManager::IsColdData(*page) &&*/
      Vfs()->GetSegmentManager().NeedInplaceUpdate(this)) {
    Vfs()->GetSegmentManager().RewriteDataPage(page, old_blk_addr);
  } else {
    block_t new_blk_addr;
    Vfs()->GetSegmentManager().WriteDataPage(this, page, &dn, old_blk_addr, &new_blk_addr);
    UpdateExtentCache(new_blk_addr, &dn);
    UpdateVersion();
  }

  F2fsPutDnode(&dn);
  return ZX_OK;
}

zx_status_t VnodeF2fs::WriteDataPage(Page *page, bool is_reclaim) {
  SuperblockInfo &superblock_info = Vfs()->GetSuperblockInfo();
  const pgoff_t end_index = (GetSize() >> kPageCacheShift);

  if (page->GetIndex() >= end_index) {
    // If the offset is out-of-range of file size,
    // this page does not have to be written to disk.
    unsigned offset = GetSize() & (kPageSize - 1);
    if ((page->GetIndex() >= end_index + 1) || !offset) {
      if (page->ClearDirtyForIo() && IsDir()) {
        superblock_info.DecreasePageCount(CountType::kDirtyDents);
        DecreaseDirtyDentries();
      } else {
        superblock_info.DecreasePageCount(CountType::kDirtyData);
      }
      return ZX_OK;
    }
    page->ZeroUserSegment(offset, kPageSize);
  }

  // TODO: Consider skipping the wb for hot/warm blocks
  // since a higher temp. block has more chances to be updated sooner.
  // if (superblock_info.IsOnRecovery()) {
  // TODO: Tracks pages skipping wb
  // ++wbc->pages_skipped;
  // page->SetDirty();
  // return kAopWritepageActivate;
  //}

  if (page->ClearDirtyForIo()) {
    fs::SharedLock rlock(superblock_info.GetFsLock(LockType::kFileOp));

    if (IsDir()) {
      superblock_info.DecreasePageCount(CountType::kDirtyDents);
      DecreaseDirtyDentries();
    } else {
      superblock_info.DecreasePageCount(CountType::kDirtyData);
    }

    if (zx_status_t err = DoWriteDataPage(page); (err != ZX_OK && err != ZX_ERR_NOT_FOUND)) {
      // TODO: Tracks pages skipping wb
      // ++wbc->pages_skipped;
      ZX_ASSERT(0);
    }
  }

  // TODO: when merge_write is available, we should flush any data pages waiting for merging.

#if 0  // porting needed
  Vfs()->GetNodeManager().ClearColdData(*page);
#endif
  return ZX_OK;
}

#if 0  // porting needed
// #define MAX_DESIRED_PAGES_WP 4096

// int VnodeF2fs::F2fsWriteDataPages(/*address_space *mapping,*/
//                                   WritebackControl *wbc) {
//   // inode *inode = mapping->host;
//   // SuperblockInfo &superblock_info = Vfs()->GetSuperblockInfo();
//   int ret;
//   // long excess_nrtw = 0, desired_nrtw;

//   // if (wbc->nr_to_write < MAX_DESIRED_PAGES_WP) {
//   //   desired_nrtw = MAX_DESIRED_PAGES_WP;
//   //   excess_nrtw = desired_nrtw - wbc->nr_to_write;
//   //   wbc->nr_to_write = desired_nrtw;
//   // }

//   // if (!IsDir())
//   //   mutex_lock(&superblock_info->writepages);
//   // ret = generic_writepages(mapping, wbc);
//   ret = 0;
//   // if (!IsDir())
//   //   mutex_unlock(&superblock_info->writepages);
//   // Vfs()->GetSegmentManager().SubmitBio(DATA, (wbc->sync_mode == WB_SYNC_ALL));

//   Vfs()->RemoveDirtyDirInode(this);

//   // wbc->nr_to_write -= excess_nrtw;
//   return ret;
// }
#endif

zx_status_t VnodeF2fs::WriteBegin(size_t pos, size_t len, fbl::RefPtr<Page> *out) {
  pgoff_t index = (static_cast<uint64_t>(pos)) >> kPageCacheShift;
  DnodeOfData dn;

  Vfs()->GetSegmentManager().BalanceFs();

  if (zx_status_t ret = GrabCachePage(index, out); ret != ZX_OK) {
    return ret;
  }

  (*out)->WaitOnWriteback();

  fs::SharedLock rlock(Vfs()->GetSuperblockInfo().GetFsLock(LockType::kFileOp));

  do {
    NodeManager::SetNewDnode(dn, this, nullptr, nullptr, 0);
    if (zx_status_t err = Vfs()->GetNodeManager().GetDnodeOfData(dn, index, 0); err != ZX_OK) {
      Page::PutPage(std::move(*out), true);
      return err;
    }

    if (dn.data_blkaddr == kNullAddr) {
      if (zx_status_t err = ReserveNewBlock(&dn); err != ZX_OK) {
        F2fsPutDnode(&dn);
        Page::PutPage(std::move(*out), true);
        return err;
      }
    }
    F2fsPutDnode(&dn);
  } while (false);

  if ((len == kPageSize) || (*out)->IsUptodate()) {
    return ZX_OK;
  }

  if (dn.data_blkaddr == kNewAddr) {
    (*out)->ZeroUserSegment(0, kPageSize);
  } else {
    if (zx_status_t err = Readpage(Vfs(), (*out).get(), dn.data_blkaddr, kReadSync); err != ZX_OK) {
      Page::PutPage(std::move(*out), true);
      return err;
    }
  }
  (*out)->SetUptodate();
  // TODO: Vfs()->GetNodeManager().ClearColdData(*pagep);
  return ZX_OK;
}

}  // namespace f2fs
