// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/f2fs/f2fs.h"

namespace f2fs {

// Lock ordering for the change of data block address:
// ->data_page
//  ->node_page
//    update block addresses in the node page
void VnodeF2fs::SetDataBlkaddr(NodePage &node_page, uint32_t ofs_in_node, block_t new_addr) {
  node_page.WaitOnWriteback();
  Node *rn = node_page.GetAddress<Node>();
  // Get physical address of data block
  uint32_t *addr_array = BlkaddrInNode(*rn);

  if (new_addr == kNewAddr) {
    ZX_DEBUG_ASSERT(addr_array[ofs_in_node] == kNullAddr);
  } else {
    ZX_DEBUG_ASSERT(addr_array[ofs_in_node] != kNullAddr);
  }

  addr_array[ofs_in_node] = CpuToLe(new_addr);
  node_page.SetDirty();
}

zx_status_t VnodeF2fs::ReserveNewBlock(NodePage &node_page, uint32_t ofs_in_node) {
  if (TestFlag(InodeInfoFlag::kNoAlloc)) {
    return ZX_ERR_ACCESS_DENIED;
  }
  if (zx_status_t ret = fs()->IncValidBlockCount(this, 1); ret != ZX_OK) {
    return ret;
  }

  SetDataBlkaddr(node_page, ofs_in_node, kNewAddr);
  MarkInodeDirty();
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

void VnodeF2fs::UpdateExtentCache(block_t blk_addr, pgoff_t file_offset) {
  InodeInfo *fi = &fi_;
  pgoff_t start_fofs, end_fofs;
  block_t start_blkaddr, end_blkaddr;

  ZX_DEBUG_ASSERT(blk_addr != kNewAddr);

  do {
    std::lock_guard ext_lock(fi->ext.ext_lock);

    start_fofs = fi->ext.fofs;
    end_fofs = fi->ext.fofs + fi->ext.len - 1;
    start_blkaddr = fi->ext.blk_addr;
    end_blkaddr = fi->ext.blk_addr + fi->ext.len - 1;

    /* Drop and initialize the matched extent */
    if (fi->ext.len == 1 && file_offset == start_fofs)
      fi->ext.len = 0;

    /* Initial extent */
    if (fi->ext.len == 0) {
      if (blk_addr != kNullAddr) {
        fi->ext.fofs = file_offset;
        fi->ext.blk_addr = blk_addr;
        fi->ext.len = 1;
      }
      break;
    }

    /* Frone merge */
    if (file_offset == start_fofs - 1 && blk_addr == start_blkaddr - 1) {
      --fi->ext.fofs;
      --fi->ext.blk_addr;
      ++fi->ext.len;
      break;
    }

    /* Back merge */
    if (file_offset == end_fofs + 1 && blk_addr == end_blkaddr + 1) {
      ++fi->ext.len;
      break;
    }

    /* Split the existing extent */
    if (fi->ext.len > 1 && file_offset >= start_fofs && file_offset <= end_fofs) {
      if ((end_fofs - file_offset) < (fi->ext.len >> 1)) {
        fi->ext.len = static_cast<uint32_t>(file_offset - start_fofs);
      } else {
        fi->ext.fofs = file_offset + 1;
        fi->ext.blk_addr = static_cast<uint32_t>(start_blkaddr + file_offset - start_fofs + 1);
        fi->ext.len -= file_offset - start_fofs + 1;
      }
      break;
    }
    return;
  } while (false);

  MarkInodeDirty();
}

zx::status<block_t> VnodeF2fs::FindDataBlkAddr(pgoff_t index) {
  uint32_t ofs_in_dnode;
  if (auto result = fs()->GetNodeManager().GetOfsInDnode(*this, index); result.is_error()) {
    return result.take_error();
  } else {
    ofs_in_dnode = result.value();
  }

  LockedPage dnode_page;
  if (zx_status_t err = fs()->GetNodeManager().FindLockedDnodePage(*this, index, &dnode_page);
      err != ZX_OK) {
    return zx::error(err);
  }

  return zx::ok(DatablockAddr(&dnode_page.GetPage<NodePage>(), ofs_in_dnode));
}

zx_status_t VnodeF2fs::FindDataPage(pgoff_t index, fbl::RefPtr<Page> *out) {
  {
    fbl::RefPtr<Page> page;
    if (zx_status_t ret = FindPage(index, &page); ret == ZX_OK) {
      if ((page)->IsUptodate()) {
        *out = std::move(page);
        return ret;
      }
    }
  }

  block_t data_blkaddr;
  if (auto result = FindDataBlkAddr(index); result.is_error()) {
    return result.error_value();
  } else {
    data_blkaddr = result.value();
  }
  if (data_blkaddr == kNullAddr)
    return ZX_ERR_NOT_FOUND;

  // By fallocate(), there is no cached page, but with kNewAddr
  if (data_blkaddr == kNewAddr)
    return ZX_ERR_INVALID_ARGS;

  LockedPage locked_page;
  if (zx_status_t err = GrabCachePage(index, &locked_page); err != ZX_OK) {
    return err;
  }

  auto page_or = fs()->MakeReadOperation(std::move(locked_page), data_blkaddr, PageType::kData);
  if (page_or.is_error()) {
    return page_or.status_value();
  }

  *out = (*page_or).release();
  return ZX_OK;
}

zx::status<LockedPagesAndAddrs> VnodeF2fs::FindDataBlockAddrsAndPages(const pgoff_t start,
                                                                      const pgoff_t end) {
  LockedPagesAndAddrs addrs_and_pages;

  ZX_DEBUG_ASSERT(end > start);

  size_t len = end - start;
  if (auto result = fs()->GetNodeManager().GetDataBlockAddresses(*this, start, len, true);
      result.is_error()) {
    return result.take_error();
  } else {
    addrs_and_pages.block_addrs = std::move(result.value());
  }
  ZX_DEBUG_ASSERT(addrs_and_pages.block_addrs.size() == len);

  std::vector<pgoff_t> page_offsets;
  page_offsets.reserve(len);
  for (auto index = start; index < end; ++index) {
    if (addrs_and_pages.block_addrs[index - start] == kNullAddr) {
      page_offsets.push_back(kInvalidPageOffset);
    } else {
      page_offsets.push_back(index);
    }
  }
  ZX_DEBUG_ASSERT(page_offsets.size() == len);

  if (auto pages_or = GrabCachePages(page_offsets); pages_or.is_error()) {
    return pages_or.take_error();
  } else {
    addrs_and_pages.pages = std::move(pages_or.value());
  }
  ZX_DEBUG_ASSERT(addrs_and_pages.pages.size() == len);
  return zx::ok(std::move(addrs_and_pages));
}

// If it tries to access a hole, return an error
// because the callers in dir.cc and gc.cc should be able to know
// whether this page exists or not.
zx_status_t VnodeF2fs::GetLockedDataPage(pgoff_t index, LockedPage *out) {
  auto page_or = GetLockedDataPages(index, index + 1);
  if (page_or.is_error()) {
    return page_or.error_value();
  }
  if (page_or->empty() || page_or.value()[0] == nullptr) {
    return ZX_ERR_NOT_FOUND;
  }

  *out = std::move(page_or.value()[0]);
  return ZX_OK;
}

zx::status<std::vector<LockedPage>> VnodeF2fs::GetLockedDataPages(const pgoff_t start,
                                                                  const pgoff_t end) {
  LockedPagesAndAddrs addrs_and_pages;
  if (auto addrs_and_pages_or = FindDataBlockAddrsAndPages(start, end);
      addrs_and_pages_or.is_error()) {
    return addrs_and_pages_or.take_error();
  } else {
    addrs_and_pages = std::move(addrs_and_pages_or.value());
  }

  if (addrs_and_pages.block_addrs.empty()) {
    return zx::ok(std::move(addrs_and_pages.pages));
  }

  auto pages_or = fs()->MakeReadOperations(std::move(addrs_and_pages.pages),
                                           std::move(addrs_and_pages.block_addrs), PageType::kData);
  if (pages_or.is_error()) {
    return pages_or.take_error();
  }

  return pages_or.take_value();
}

// Caller ensures that this data page is never allocated.
// A new zero-filled data page is allocated in the page cache.
zx_status_t VnodeF2fs::GetNewDataPage(pgoff_t index, bool new_i_size, LockedPage *out) {
  block_t data_blkaddr;
  {
    LockedPage dnode_page;
    if (zx_status_t err = fs()->GetNodeManager().GetLockedDnodePage(*this, index, &dnode_page);
        err != ZX_OK) {
      return err;
    }

    uint32_t ofs_in_dnode;
    if (auto result = fs()->GetNodeManager().GetOfsInDnode(*this, index); result.is_error()) {
      return result.error_value();
    } else {
      ofs_in_dnode = result.value();
    }

    data_blkaddr = DatablockAddr(&dnode_page.GetPage<NodePage>(), ofs_in_dnode);
    if (data_blkaddr == kNullAddr) {
      if (zx_status_t ret = ReserveNewBlock(dnode_page.GetPage<NodePage>(), ofs_in_dnode);
          ret != ZX_OK) {
        return ret;
      }
      data_blkaddr = kNewAddr;
    }
  }

  LockedPage page;
  if (zx_status_t ret = GrabCachePage(index, &page); ret != ZX_OK) {
    return ret;
  }

  if (page->IsUptodate()) {
    *out = std::move(page);
    return ZX_OK;
  }

  auto page_or = fs()->MakeReadOperation(std::move(page), data_blkaddr, PageType::kData);
  if (page_or.is_error()) {
    return page_or.status_value();
  }

  if (new_i_size && GetSize() < ((index + 1) << kPageCacheShift)) {
    SetSize((index + 1) << kPageCacheShift);
    // TODO: mark sync when fdatasync is available.
    SetFlag(InodeInfoFlag::kUpdateDir);
    MarkInodeDirty();
  }

  *out = std::move(*page_or);
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
//   unsigned maxblocks = bh_result.value().b_size > blkbits;
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
//   // TODO(unknown): should be replaced with NodeManager->GetDnodeOfData
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

zx_status_t VnodeF2fs::DoWriteDataPage(LockedPage &page) {
  LockedPage dnode_page;
  if (zx_status_t err =
          fs()->GetNodeManager().FindLockedDnodePage(*this, page->GetIndex(), &dnode_page);
      err != ZX_OK) {
    return err;
  }

  uint32_t ofs_in_dnode;
  if (auto result = fs()->GetNodeManager().GetOfsInDnode(*this, page->GetIndex());
      result.is_error()) {
    return result.error_value();
  } else {
    ofs_in_dnode = result.value();
  }

  block_t old_blk_addr = DatablockAddr(&dnode_page.GetPage<NodePage>(), ofs_in_dnode);
  // This page is already truncated
  if (old_blk_addr == kNullAddr) {
    return ZX_ERR_NOT_FOUND;
  }

  // If current allocation needs SSR,
  // it had better in-place writes for updated data.
  if (old_blk_addr != kNewAddr && !page->IsColdData() &&
      fs()->GetSegmentManager().NeedInplaceUpdate(this)) {
    fs()->GetSegmentManager().RewriteDataPage(page, old_blk_addr);
  } else {
    block_t new_blk_addr;
    pgoff_t file_offset = page->GetIndex();
    fs()->GetSegmentManager().WriteDataPage(this, page, dnode_page.GetPage<NodePage>().NidOfNode(),
                                            ofs_in_dnode, old_blk_addr, &new_blk_addr);
    SetDataBlkaddr(dnode_page.GetPage<NodePage>(), ofs_in_dnode, new_blk_addr);
    UpdateExtentCache(new_blk_addr, file_offset);
    UpdateVersion();
  }

  return ZX_OK;
}

zx_status_t VnodeF2fs::WriteDataPage(LockedPage &page, bool is_reclaim) {
  const pgoff_t end_index = (GetSize() >> kPageCacheShift);

  if (page->GetIndex() >= end_index) {
    // If the offset is out-of-range of file size,
    // this page does not have to be written to disk.
    unsigned offset = GetSize() & (kPageSize - 1);
    if ((page->GetIndex() >= end_index + 1) || !offset) {
      if (page->ClearDirtyForIo()) {
        page->SetWriteback();
      }
      return ZX_ERR_OUT_OF_RANGE;
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
    page->SetWriteback();
    if (zx_status_t err = DoWriteDataPage(page); err != ZX_OK) {
      // TODO: Tracks pages skipping wb
      // ++wbc->pages_skipped;
      return err;
    }
  }

  return ZX_OK;
}

zx::status<std::vector<LockedPage>> VnodeF2fs::WriteBegin(const size_t offset, const size_t len) {
  fs()->GetSegmentManager().BalanceFs();

  const pgoff_t index_start = safemath::CheckDiv<pgoff_t>(offset, kBlockSize).ValueOrDie();
  const size_t offset_end = safemath::CheckAdd<size_t>(offset, len).ValueOrDie();
  const pgoff_t index_end = CheckedDivRoundUp<pgoff_t>(offset_end, kBlockSize);

  std::vector<LockedPage> data_pages;
  data_pages.reserve(index_end - index_start);
  if (auto pages_or = GrabCachePages(index_start, index_end); pages_or.is_error()) {
    return pages_or.take_error();
  } else {
    data_pages = std::move(pages_or.value());
  }

  fs::SharedLock rlock(fs()->GetSuperblockInfo().GetFsLock(LockType::kFileOp));

  for (auto &page : data_pages) {
    page->WaitOnWriteback();
  }

  std::vector<block_t> data_block_addresses;
  if (auto result =
          fs()->GetNodeManager().GetDataBlockAddresses(*this, index_start, index_end - index_start);
      result.is_error()) {
    return result.take_error();
  } else {
    data_block_addresses = std::move(result.value());
  }

  std::vector<LockedPage> read_pages;
  std::vector<block_t> read_addrs;
  bool read_front_page = false, read_rear_page = false;
  if (offset % kBlockSize) {
    read_front_page = true;
    read_pages.push_back(std::move(data_pages.front()));
    read_addrs.emplace_back(data_block_addresses.front());
  }
  if (offset_end % kBlockSize) {
    if (data_pages.size() > 1 || !read_front_page) {
      read_rear_page = true;
      read_pages.push_back(std::move(data_pages.back()));
      read_addrs.emplace_back(data_block_addresses.back());
    }
  }
  if (read_pages.size()) {
    auto pages_or =
        fs()->MakeReadOperations(std::move(read_pages), std::move(read_addrs), PageType::kData);
    if (pages_or.is_error()) {
      return pages_or.take_error();
    }
    if (read_front_page) {
      data_pages.front() = std::move((*pages_or).front());
    }
    if (read_rear_page) {
      data_pages.back() = std::move((*pages_or).back());
    }
  }

  return zx::ok(std::move(data_pages));
}

zx_status_t VnodeF2fs::WriteDirtyPage(LockedPage &page, bool is_reclaim) {
  if (IsMeta()) {
    return fs()->F2fsWriteMetaPage(page, is_reclaim);
  } else if (IsNode()) {
    return fs()->GetNodeManager().F2fsWriteNodePage(page, is_reclaim);
  }
  return WriteDataPage(page, false);
}

}  // namespace f2fs
