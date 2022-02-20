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

  if (zx_status_t err = Vfs()->MakeOperation(storage::OperationType::kRead, *out, dn.data_blkaddr,
                                             PageType::kData);
      err != ZX_OK) {
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

  if (zx_status_t err = Vfs()->MakeOperation(storage::OperationType::kRead, *out, dn.data_blkaddr,
                                             PageType::kData);
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
    if (zx_status_t err = Vfs()->MakeOperation(storage::OperationType::kRead, *out, dn.data_blkaddr,
                                               PageType::kData);
        err != ZX_OK) {
      Page::PutPage(std::move(*out), true);
      return err;
    }
  }
  (*out)->SetUptodate();

  if (new_i_size && GetSize() < ((index + 1) << kPageCacheShift)) {
    SetSize((index + 1) << kPageCacheShift);
    // TODO: mark sync when fdatasync is available.
    MarkInodeDirty();
  }

  return ZX_OK;
}

#if 0  // porting needed
/**
 * This function should be used by the data read flow only where it
 * does not check the "create" flag that indicates block allocation.
 * The reason for this special functionality is to exploit VFS readahead
 * mechanism.
 */
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
#endif

zx_status_t VnodeF2fs::DoWriteDataPage(fbl::RefPtr<Page> page) {
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
    return ZX_ERR_NOT_FOUND;
  }

  // If current allocation needs SSR,
  // it had better in-place writes for updated data.
  // TODO: GC, Impl IsCodeData
  if (old_blk_addr != kNewAddr && /*! NodeManager::IsColdData(*page) &&*/
      Vfs()->GetSegmentManager().NeedInplaceUpdate(this)) {
    Vfs()->GetSegmentManager().RewriteDataPage(std::move(page), old_blk_addr);
  } else {
    block_t new_blk_addr;
    Vfs()->GetSegmentManager().WriteDataPage(this, std::move(page), &dn, old_blk_addr,
                                             &new_blk_addr);
    UpdateExtentCache(new_blk_addr, &dn);
    UpdateVersion();
  }

  F2fsPutDnode(&dn);
  return ZX_OK;
}

zx_status_t VnodeF2fs::WriteDataPage(fbl::RefPtr<Page> page, bool is_reclaim) {
  SuperblockInfo &superblock_info = Vfs()->GetSuperblockInfo();
  const pgoff_t end_index = (GetSize() >> kPageCacheShift);

  if (page->GetIndex() >= end_index) {
    // If the offset is out-of-range of file size,
    // this page does not have to be written to disk.
    unsigned offset = GetSize() & (kPageSize - 1);
    if ((page->GetIndex() >= end_index + 1) || !offset) {
      if (page->ClearDirtyForIo(true)) {
        page->SetWriteback();
      }
      return ZX_ERR_OUT_OF_RANGE;
    }
    // Writeback Pages for dir/vnode do not have mappings.
    page->Map();
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

  if (page->ClearDirtyForIo(true)) {
    page->SetWriteback();
    fs::SharedLock rlock(superblock_info.GetFsLock(LockType::kFileOp));
    if (zx_status_t err = DoWriteDataPage(std::move(page)); err != ZX_OK) {
      // TODO: Tracks pages skipping wb
      // ++wbc->pages_skipped;
      return err;
    }
  }

#if 0  // TODO: impl it, GC
  Vfs()->GetNodeManager().ClearColdData(*page);
#endif
  return ZX_OK;
}

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
    if (zx_status_t err = Vfs()->MakeOperation(storage::OperationType::kRead, *out, dn.data_blkaddr,
                                               PageType::kData);
        err != ZX_OK) {
      Page::PutPage(std::move(*out), true);
      return err;
    }
  }
  (*out)->SetUptodate();
  // TODO: GC, Vfs()->GetNodeManager().ClearColdData(*pagep);
  return ZX_OK;
}

zx_status_t VnodeF2fs::WriteDirtyPage(fbl::RefPtr<Page> page, bool is_reclaim) {
  if (IsMeta()) {
    return Vfs()->F2fsWriteMetaPage(std::move(page), false);
  } else if (IsNode()) {
    return Vfs()->GetNodeManager().F2fsWriteNodePage(std::move(page), false);
  }
  return WriteDataPage(std::move(page), false);
}

}  // namespace f2fs
