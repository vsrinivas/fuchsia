// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/f2fs/f2fs.h"

namespace f2fs {

/**
 * Lock ordering for the change of data block address:
 * ->data_page
 *  ->node_page
 *    update block addresses in the node page
 */
void VnodeF2fs::SetDataBlkaddr(DnodeOfData *dn, block_t new_addr) {
  Node *rn;
  uint32_t *addr_array;
  Page *node_page = dn->node_page;
  unsigned int ofs_in_node = dn->ofs_in_node;

  WaitOnPageWriteback(node_page);

  rn = static_cast<Node *>(PageAddress(node_page));

  /* Get physical address of data block */
  addr_array = BlkaddrInNode(rn);
  addr_array[ofs_in_node] = CpuToLe(new_addr);
#if 0  // porting needed
  // set_page_dirty(node_page);
#else
  FlushDirtyNodePage(Vfs(), node_page);
#endif
}

zx_status_t VnodeF2fs::ReserveNewBlock(DnodeOfData *dn) {
  if (dn->vnode->TestFlag(InodeInfoFlag::kNoAlloc))
    return ZX_ERR_ACCESS_DENIED;
  if (zx_status_t ret = Vfs()->IncValidBlockCount(dn->vnode, 1); ret != ZX_OK)
    return ret;

  SetDataBlkaddr(dn, kNewAddr);
  dn->data_blkaddr = kNewAddr;
  Vfs()->Nodemgr().SyncInodePage(dn);
  return ZX_OK;
}

#if 0  // porting needed
// int VnodeF2fs::CheckExtentCache(inode *inode, pgoff_t pgofs,
//           buffer_head *bh_result)
// {
//   Inode_info *fi = F2FS_I(inode);
//   //SbInfo *sbi = F2FS_SB(inode->i_sb);
//   SbInfo *sbi = F2FS_SB(inode->i_sb);
//   pgoff_t start_fofs, end_fofs;
//   block_t start_blkaddr;

//   ReadLock(&fi->ext.ext_lock);
//   if (fi->ext.len == 0) {
//     ReadUnlock(&fi->ext.ext_lock);
//     return 0;
//   }

//   sbi->total_hit_ext++;
//   start_fofs = fi->ext.fofs;
//   end_fofs = fi->ext.fofs + fi->ext.len - 1;
//   start_blkaddr = fi->ext.blk_addr;

//   if (pgofs >= start_fofs && pgofs <= end_fofs) {
//     unsigned int blkbits = inode->i_sb->s_blocksize_bits;
//     size_t count;

//     clear_buffer_new(bh_result);
//     map_bh(bh_result, inode->i_sb,
//        start_blkaddr + pgofs - start_fofs);
//     count = end_fofs - pgofs + 1;
//     if (count < (UINT_MAX >> blkbits))
//       bh_result->b_size = (count << blkbits);
//     else
//       bh_result->b_size = UINT_MAX;

//     sbi->read_hit_ext++;
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
  fofs = Vfs()->Nodemgr().StartBidxOfNode(dn->node_page) + dn->ofs_in_node;

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
      fi->ext.fofs--;
      fi->ext.blk_addr--;
      fi->ext.len++;
      break;
    }

    /* Back merge */
    if (fofs == end_fofs + 1 && blk_addr == end_blkaddr + 1) {
      fi->ext.len++;
      break;
    }

    /* Split the existing extent */
    if (fi->ext.len > 1 && fofs >= start_fofs && fofs <= end_fofs) {
      if ((end_fofs - fofs) < (fi->ext.len >> 1)) {
        fi->ext.len = fofs - start_fofs;
      } else {
        fi->ext.fofs = fofs + 1;
        fi->ext.blk_addr = start_blkaddr + fofs - start_fofs + 1;
        fi->ext.len -= fofs - start_fofs + 1;
      }
      break;
    }
    return;
  } while (false);

  Vfs()->Nodemgr().SyncInodePage(dn);
}

zx_status_t VnodeF2fs::FindDataPage(pgoff_t index, Page **out) {
#if 0  // porting needed
  // address_space *mapping = inode->i_mapping;
#endif
  DnodeOfData dn;
  Page *page = nullptr;

#if 0  // porting needed
  // page = FindGetPage(mapping, index);
  // if (page && PageUptodate(page))
  //   return page;
  // F2fsPutPage(page, 0);
#endif

  SetNewDnode(&dn, this, NULL, NULL, 0);
  if (zx_status_t err = Vfs()->Nodemgr().GetDnodeOfData(&dn, index, kRdOnlyNode); err != ZX_OK)
    return err;
  F2fsPutDnode(&dn);

  if (dn.data_blkaddr == kNullAddr)
    return ZX_ERR_NOT_FOUND;

  /* By fallocate(), there is no cached page, but with kNewAddr */
  if (dn.data_blkaddr == kNewAddr)
    return ZX_ERR_INVALID_ARGS;

  if (page = GrabCachePage(this, ino_, index); page == nullptr)
    return ZX_ERR_NO_MEMORY;

  if (zx_status_t err = Readpage(Vfs(), page, dn.data_blkaddr, kReadSync); err != ZX_OK) {
    F2fsPutPage(page, 1);
    return err;
  }
#if 0  // porting needed
  // unlock_page(page);
#endif
  *out = page;
  return ZX_OK;
}

/**
 * If it tries to access a hole, return an error.
 * Because, the callers, functions in dir.c and GC, should be able to know
 * whether this page exists or not.
 */
zx_status_t VnodeF2fs::GetLockDataPage(pgoff_t index, Page **out) {
  DnodeOfData dn;
  Page *page;

  page = nullptr;

  SetNewDnode(&dn, this, NULL, NULL, 0);
  if (zx_status_t err = Vfs()->Nodemgr().GetDnodeOfData(&dn, index, kRdOnlyNode); err != ZX_OK)
    return err;
  F2fsPutDnode(&dn);

  if (dn.data_blkaddr == kNullAddr) {
    return ZX_ERR_NOT_FOUND;
  }

  if (page = GrabCachePage(this, ino_, index); page == nullptr)
    return ZX_ERR_NO_MEMORY;

  if (PageUptodate(page)) {
    *out = page;
    return ZX_OK;
  }

  ZX_ASSERT(dn.data_blkaddr != kNewAddr);
  ZX_ASSERT(dn.data_blkaddr != kNullAddr);

  if (zx_status_t err = VnodeF2fs::Readpage(Vfs(), page, dn.data_blkaddr, kReadSync);
      err != ZX_OK) {
    F2fsPutPage(page, 1);
    return err;
  }
  *out = page;
  return ZX_OK;
}

/**
 * Caller ensures that this data page is never allocated.
 * A new zero-filled data page is allocated in the page cache.
 */
zx_status_t VnodeF2fs::GetNewDataPage(pgoff_t index, bool new_i_size, Page **out) {
#if 0  // porting needed
  // address_space *mapping = inode->i_mapping;
#endif
  Page *page;
  DnodeOfData dn;

  SetNewDnode(&dn, this, NULL, NULL, 0);
  if (zx_status_t err = Vfs()->Nodemgr().GetDnodeOfData(&dn, index, 0); err != ZX_OK)
    return err;

  if (dn.data_blkaddr == kNullAddr) {
    if (zx_status_t ret = ReserveNewBlock(&dn); ret != ZX_OK) {
      F2fsPutDnode(&dn);
      return ret;
    }
  }
  F2fsPutDnode(&dn);

  if (page = GrabCachePage(this, ino_, index); page == nullptr)
    return ZX_ERR_NO_MEMORY;

  if (PageUptodate(page)) {
    *out = page;
    return ZX_OK;
  }

  if (dn.data_blkaddr == kNewAddr) {
    ZeroUserSegment(page, 0, kPageCacheSize);
  } else {
    if (zx_status_t err = Readpage(Vfs(), page, dn.data_blkaddr, kReadSync); err != ZX_OK) {
      F2fsPutPage(page, 1);
      return err;
    }
  }
  SetPageUptodate(page);

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

  *out = page;
  return ZX_OK;
}

#if 0  // porting needed
// static void read_end_io(bio *bio, int err)
// {
//   const int uptodate = test_bit(BIO_UPTODATE, &bio->bi_flags);
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
  //   block_device *bdev = sbi->sb->s_bdev;
  //   bool sync = (type == kReadSync);
  //   bio *bio;

  //   /* This page can be already read by other threads */
  //   if (PageUptodate(page)) {
  //     if (!sync)
  //       unlock_page(page);
  //     return 0;
  //   }

  //   down_read(&sbi->bio_sem);

  //   /* Allocate a new bio */
  //   bio = f2fs_bio_alloc(bdev, blk_addr << (sbi->log_blocksize - 9),
  //         1, GFP_NOFS | __GFP_HIGH);

  //   /* Initialize the bio */
  //   bio->bi_end_io = read_end_io;
  //   if (bio_add_page(bio, page, kPageCacheSize, 0) < kPageCacheSize) {
  //     kfree(bio->bi_private);
  //     bio_put(bio);
  //     up_read(&sbi->bio_sem);
  //     return -EFAULT;
  //   }

  //   submit_bio(type, bio);
  //   up_read(&sbi->bio_sem);

  //   /* wait for read completion if sync */
  //   if (sync) {
  //     lock_page(page);
  //     if (PageError(page))
  //       return -EIO;
  //   }
  // return 0;
#else
  return fs->GetBc().Readblk(blk_addr, page->data);
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
//   unsigned int blkbits = inode->i_sb->s_blocksize_bits;
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
//   //SetNewDnode(&dn, inode, NULL, NULL, 0);
//   // TODO(unknown): shoud be replaced with NodeMgr->GetDnodeOfData
//   /*err = get_DnodeOfData(&dn, pgofs, kRdOnlyNode);
//   if (err)
//     return (err == ZX_ERR_NOT_FOUND) ? 0 : err; */

//   /* It does not support data allocation */
//   ZX_ASSERT(!create);

//   if (dn.data_blkaddr != kNewAddr && dn.data_blkaddr != kNullAddr) {
//     unsigned int i;
//     unsigned int end_offset;

//     end_offset = IsInode(dn.node_page) ?
//         kAddrsPerInode :
//         kAddrsPerBlock;

//     clear_buffer_new(bh_result);

//     /* Give more consecutive addresses for the read ahead */
//     for (i = 0; i < end_offset - dn.ofs_in_node; i++)
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
#if 0  // porting needed
  // inode *inode = page->mapping->host;
#endif
  SbInfo &sbi = Vfs()->GetSbInfo();
  block_t old_blk_addr, new_blk_addr;
  DnodeOfData dn;

  SetNewDnode(&dn, this, NULL, NULL, 0);
  if (zx_status_t err = Vfs()->Nodemgr().GetDnodeOfData(&dn, page->index, kRdOnlyNode);
      err != ZX_OK)
    return err;

  old_blk_addr = dn.data_blkaddr;

  /* This page is already truncated */
  if (old_blk_addr == kNullAddr) {
    F2fsPutDnode(&dn);
    return ZX_OK;
  }

  SetPageWriteback(page);

  /*
   * If current allocation needs SSR,
   * it had better in-place writes for updated data.
   */
  if (old_blk_addr != kNewAddr && !Vfs()->Nodemgr().IsColdData(page) &&
      Vfs()->Segmgr().NeedInplaceUpdate(this)) {
    Vfs()->Segmgr().RewriteDataPage(page, old_blk_addr);
  } else {
    Vfs()->Segmgr().WriteDataPage(this, page, &dn, old_blk_addr, &new_blk_addr);
    UpdateExtentCache(new_blk_addr, &dn);
    fi_.data_version = LeToCpu(GetCheckpoint(&sbi)->checkpoint_ver);
  }

  F2fsPutDnode(&dn);
  return ZX_OK;
}

zx_status_t VnodeF2fs::WriteDataPageReq(Page *page, WritebackControl *wbc) {
  SbInfo &sbi = Vfs()->GetSbInfo();
  const pgoff_t end_index = (GetSize() >> kPageCacheShift);
  unsigned offset;
  zx_status_t err;

  if (page->index >= end_index) {
    /*
     * If the offset is out-of-range of file size,
     * this page does not have to be written to disk.
     */
    offset = GetSize() & (kPageCacheSize - 1);
    if ((page->index >= end_index + 1) || !offset) {
      if (IsDir()) {
        DecPageCount(&sbi, CountType::kDirtyDents);
#if 0  // porting needed
       // inode_dec_dirty_dents(inode);
#endif
      }

#if 0  // porting needed
      // unlock_page(page);
#endif
      return ZX_OK;
    }

    ZeroUserSegment(page, offset, kPageCacheSize);
  }

  if (sbi.por_doing) {
#if 0  // porting needed
    // wbc->pages_skipped++;
    // set_page_dirty(page);
#else
    FlushDirtyDataPage(Vfs(), page);
#endif
    return kAopWritepageActivate;
  }

#if 0  // porting needed
  // if (wbc->for_reclaim && !IsDir() && !Vfs()->Nodemgr().IsColdData(page))
  //   goto redirty_out;
#endif

  do {
    fs::SharedLock rlock(sbi.fs_lock[static_cast<int>(LockType::kFileOp)]);
    if (IsDir()) {
      DecPageCount(&sbi, CountType::kDirtyDents);
#if 0  // porting needed
      // inode_dec_dirty_dents(inode);
#endif
    }

    if (err = DoWriteDataPage(page); (err != ZX_OK && err != ZX_ERR_NOT_FOUND)) {
#if 0  // porting needed
      // wbc->pages_skipped++;
      // set_page_dirty(page);
#endif
      ZX_ASSERT(0);
    }
  } while (false);

#if 0  // porting needed
  // if (wbc->for_reclaim)
  //   f2fs_submit_bio(sbi, DATA, true);
#endif

  if (err == ZX_ERR_NOT_FOUND) {
#if 0  // porting needed
    // unlock_page(page);
#endif
    return ZX_OK;
  }

  Vfs()->Nodemgr().ClearColdData(page);
#if 0  // porting needed
  // unlock_page(page);

  // if (!wbc->for_reclaim && !IsDir())
  //   fs->Segmgr().BalanceFs();
#endif
  return ZX_OK;
}

#if 0  // porting needed
// #define MAX_DESIRED_PAGES_WP 4096

// int VnodeF2fs::F2fsWriteDataPages(/*address_space *mapping,*/
//                                   WritebackControl *wbc) {
//   // inode *inode = mapping->host;
//   // SbInfo &sbi = Vfs()->GetSbInfo();
//   int ret;
//   // long excess_nrtw = 0, desired_nrtw;

//   // if (wbc->nr_to_write < MAX_DESIRED_PAGES_WP) {
//   //   desired_nrtw = MAX_DESIRED_PAGES_WP;
//   //   excess_nrtw = desired_nrtw - wbc->nr_to_write;
//   //   wbc->nr_to_write = desired_nrtw;
//   // }

//   // if (!IsDir())
//   //   mutex_lock(&sbi->writepages);
//   // ret = generic_writepages(mapping, wbc);
//   ret = 0;
//   // if (!IsDir())
//   //   mutex_unlock(&sbi->writepages);
//   // Vfs()->Segmgr().SubmitBio(DATA, (wbc->sync_mode == WB_SYNC_ALL));

//   Vfs()->RemoveDirtyDirInode(this);

//   // wbc->nr_to_write -= excess_nrtw;
//   return ret;
// }
#endif

zx_status_t VnodeF2fs::WriteBegin(size_t pos, size_t len, Page **pagep) {
  SbInfo &sbi = Vfs()->GetSbInfo();
  pgoff_t index = (static_cast<uint64_t>(pos)) >> kPageCacheShift;
  DnodeOfData dn;

  Vfs()->Segmgr().BalanceFs();

#if 0  // porting needed
  // page = GrabCachePage_write_begin(/*mapping,*/ index, flags);
#else
  *pagep = GrabCachePage(this, ino_, index);
  if (!*pagep)
    return ZX_ERR_NO_MEMORY;
#endif

  fs::SharedLock rlock(sbi.fs_lock[static_cast<int>(LockType::kFileOp)]);
  std::lock_guard write_lock(io_lock_);
  do {
    SetNewDnode(&dn, this, NULL, NULL, 0);
    if (zx_status_t err = Vfs()->Nodemgr().GetDnodeOfData(&dn, index, 0); err != ZX_OK) {
      F2fsPutPage(*pagep, 1);
      return err;
    }

    if (dn.data_blkaddr == kNullAddr) {
      if (zx_status_t err = ReserveNewBlock(&dn); err != ZX_OK) {
        F2fsPutDnode(&dn);
        F2fsPutPage(*pagep, 1);
        return err;
      }
    }
    F2fsPutDnode(&dn);
  } while (false);

  if ((len == kPageCacheSize) || PageUptodate(*pagep))
    return ZX_OK;

  if (dn.data_blkaddr == kNewAddr) {
    ZeroUserSegment(*pagep, 0, kPageCacheSize);
  } else {
    if (zx_status_t err = Readpage(Vfs(), *pagep, dn.data_blkaddr, kReadSync); err != ZX_OK) {
      F2fsPutPage(*pagep, 1);
      return err;
    }
  }
  SetPageUptodate(*pagep);
  Vfs()->Nodemgr().ClearColdData(*pagep);
  return ZX_OK;
}

#if 0  // porting needed
// ssize_t VnodeF2fs::F2fsDirectIO(/*int rw, kiocb *iocb,
//     const iovec *iov, */ loff_t offset, uint64_t nr_segs) {
//   // file *file = iocb->ki_filp;
//   // inode *inode = file->f_mapping->host;

//   // if (rw == kWrite)
//   //   return 0;

//   // /* Needs synchronization with the cleaner */
//   // return blockdev_direct_IO(rw, iocb, inode, iov, offset, nr_segs,
//   //             get_data_block_ro);
//   return 0;
// }

// void VnodeF2fs::F2fsInvalidateDataPage(Page *page, uint64_t offset)
// {
//   // inode *inode = page->mapping->host;
//   inode *inode = new inode();
//   SbInfo *sbi = F2FS_SB(inode->i_sb);
//   if (inode->IsDir() && PageDirty(page)) {
//     DecPageCount(sbi, CountType::kDirtyDents);
//     InodeDecDirtyDents(inode);
//   }
//   ClearPagePrivate(page);
// }

// int VnodeF2fs::F2fsReleaseDataPage(Page *page, gfp_t wait)
// {
//   ClearPagePrivate(page);
//   return 0;
// }

// int VnodeF2fs::F2fsSetDataPageDirty(Page *page) {
//   SetPageUptodate(page);
//   if (!PageDirty(page)) {
//     // __set_page_dirty_nobuffers(page);
//     FlushDirtyDataPage(Vfs(), page);
//     Vfs()->SetDirtyDirPage(this, page);
//     return 1;
//   }
//   return 0;
// }
#endif

}  // namespace f2fs
