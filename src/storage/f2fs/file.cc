// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <algorithm>

#include "src/storage/f2fs/f2fs.h"

namespace f2fs {

File::File(F2fs *fs) : VnodeF2fs(fs) {}

File::File(F2fs *fs, ino_t ino) : VnodeF2fs(fs, ino) {}

#if 0  // porting needed
// int File::F2fsVmPageMkwrite(vm_area_struct *vma, vm_fault *vmf) {
//   return 0;
//   //   Page *page = vmf->page;
//   //   VnodeF2fs *vnode = this;
//   //   // SbInfo &sbi = Vfs()->GetSbInfo();
//   //   Page *node_page;
//   //   block_t old_blk_addr;
//   //   DnodeOfData dn;
//   //   int err;

//   //   Vfs()->GetSegmentManager().BalanceFs();

//   //   sb_start_pagefault(nullptr);

//   //   // mutex_lock_op(sbi, LockType::kDataNew);

//   //   /* block allocation */
//   //   SetNewDnode(&dn, vnode, nullptr, nullptr, 0);
//   //   err = Vfs()->GetNodeManager().GetDnodeOfData(&dn, page->index, 0);
//   //   if (err) {
//   //     // mutex_unlock_op(sbi, LockType::kDataNew);
//   //     goto out;
//   //   }

//   //   old_blk_addr = dn.data_blkaddr;
//   //   node_page = dn.node_page;

//   //   if (old_blk_addr == kNullAddr) {
//   //     err = VnodeF2fs::ReserveNewBlock(&dn);
//   //     if (err) {
//   //       F2fsPutDnode(&dn);
//   //       // mutex_unlock_op(sbi, LockType::kDataNew);
//   //       goto out;
//   //     }
//   //   }
//   //   F2fsPutDnode(&dn);

//   //   // mutex_unlock_op(sbi, LockType::kDataNew);

//   //   // lock_page(page);
//   //   // if (page->mapping != inode->i_mapping ||
//   //   //     page_offset(page) >= i_size_read(inode) ||
//   //   //     !PageUptodate(page)) {
//   //   //   unlock_page(page);
//   //   //   err = -EFAULT;
//   //   //   goto out;
//   //   // }

//   //   /*
//   //    * check to see if the page is mapped already (no holes)
//   //    */
//   //   if (PageMappedToDisk(page))
//   //     goto out;

//   //   /* fill the page */
//   //   WaitOnPageWriteback(page);

//   //   /* page is wholly or partially inside EOF */
//   //   if (((page->index + 1) << kPageCacheShift) > i_size) {
//   //     unsigned offset;
//   //     offset = i_size & ~PAGE_CACHE_MASK;
//   //     ZeroUserSegment(page, offset, kPageCacheSize);
//   //   }
//   //   // set_page_dirty(page);
//   //   FlushDirtyDataPage(Vfs(), page);
//   //   SetPageUptodate(page);

//   //   file_update_time(vma->vm_file);
//   // out:
//   //   sb_end_pagefault(nullptr);
//   //   return block_page_mkwrite_return(err);
// }
#endif

#if 0  // porting needed
// int File::F2fsFileMmap(/*file *file,*/ vm_area_struct *vma) {
//   // file_accessed(file);
//   // vma->vm_ops = &f2fs_file_vm_ops;
//   return 0;
// }

// void File::FillZero(pgoff_t index, loff_t start, loff_t len) {
//   Page *page = nullptr;
//   zx_status_t err;

//   if (!len)
//     return;

//   err = GetNewDataPage(index, false, page);

//   if (!err) {
//     WaitOnPageWriteback(page);
//     zero_user(page, start, len);
// #if 0  // porting needed
//     // set_page_dirty(page);
// #else
//     FlushDirtyDataPage(Vfs(), page);
// #endif
//     F2fsPutPage(page, 1);
//   }
// }

// int File::PunchHole(loff_t offset, loff_t len, int mode) {
//   pgoff_t pg_start, pg_end;
//   loff_t off_start, off_end;
//   int ret = 0;

//   pg_start = ((uint64_t)offset) >> kPageCacheShift;
//   pg_end = ((uint64_t)offset + len) >> kPageCacheShift;

//   off_start = offset & (kPageCacheSize - 1);
//   off_end = (offset + len) & (kPageCacheSize - 1);

//   if (pg_start == pg_end) {
//     FillZero(pg_start, off_start, off_end - off_start);
//   } else {
//     if (off_start)
//       FillZero(pg_start++, off_start, kPageCacheSize - off_start);
//     if (off_end)
//       FillZero(pg_end, 0, off_end);

//     if (pg_start < pg_end) {
//       loff_t blk_start, blk_end;

//       blk_start = pg_start << kPageCacheShift;
//       blk_end = pg_end << kPageCacheShift;
// #if 0  // porting needed
//       // truncate_inode_pages_range(nullptr, blk_start,
//       //     blk_end - 1);
// #endif
//       ret = TruncateHole(pg_start, pg_end);
//     }
//   }

//   if (!(mode & FALLOC_FL_KEEP_SIZE) && i_size <= (offset + len)) {
//     i_size = offset;
//     MarkInodeDirty();
//   }

//   return ret;
// }

// int File::ExpandInodeData(loff_t offset, off_t len, int mode) {
//   SbInfo &sbi = Vfs()->GetSbInfo();
//   pgoff_t index, pg_start, pg_end;
//   loff_t new_size = i_size;
//   loff_t off_start, off_end;
//   int ret = 0;

// #if 0  // porting needed
//   // ret = inode_newsize_ok(this, (len + offset));
//   // if (ret)
//   //   return ret;
// #endif

//   pg_start = ((uint64_t)offset) >> kPageCacheShift;
//   pg_end = ((uint64_t)offset + len) >> kPageCacheShift;

//   off_start = offset & (kPageCacheSize - 1);
//   off_end = (offset + len) & (kPageCacheSize - 1);

//   for (index = pg_start; index <= pg_end; index++) {
//     DnodeOfData dn;

//     mutex_lock_op(&sbi, LockType::kDataNew);

//     SetNewDnode(&dn, this, nullptr, nullptr, 0);
//     ret = Vfs()->GetNodeManager().GetDnodeOfData(&dn, index, 0);
//     if (ret) {
//       mutex_unlock_op(&sbi, LockType::kDataNew);
//       break;
//     }

//     if (dn.data_blkaddr == kNullAddr) {
//       ret = ReserveNewBlock(&dn);
//       if (ret) {
//         F2fsPutDnode(&dn);
//         mutex_unlock_op(&sbi, LockType::kDataNew);
//         break;
//       }
//     }
//     F2fsPutDnode(&dn);

//     mutex_unlock_op(&sbi, LockType::kDataNew);

//     if (pg_start == pg_end)
//       new_size = offset + len;
//     else if (index == pg_start && off_start)
//       new_size = (index + 1) << kPageCacheShift;
//     else if (index == pg_end)
//       new_size = (index << kPageCacheShift) + off_end;
//     else
//       new_size += kPageCacheSize;
//   }

//   if (!(mode & FALLOC_FL_KEEP_SIZE) && i_size < new_size) {
//     i_size = new_size;
//     MarkInodeDirty();
//   }

//   return ret;
// }

// long File::F2fsFallocate(int mode, loff_t offset, loff_t len) {
//   long ret;

//   if (mode & ~(FALLOC_FL_KEEP_SIZE | FALLOC_FL_PUNCH_HOLE))
//     return -EOPNOTSUPP;

//   if (mode & FALLOC_FL_PUNCH_HOLE)
//     ret = PunchHole(offset, len, mode);
//   else
//     ret = ExpandInodeData(offset, len, mode);

//   Vfs()->GetSegmentManager().BalanceFs();
//   return ret;
// }

// #define F2FS_REG_FLMASK    (~(FS_DIRSYNC_FL | FS_TOPDIR_FL))
// #define F2FS_OTHER_FLMASK  (FS_NODUMP_FL | FS_NOATIME_FL)

// inline uint32_t File::F2fsMaskFlags(umode_t mode, uint32_t flags) {
//   if (S_ISDIR(mode))
//     return flags;
//   else if (S_ISREG(mode))
//     return flags & F2FS_REG_FLMASK;
//   else
//     return flags & F2FS_OTHER_FLMASK;
// }

// long File::F2fsIoctl(/*file *filp,*/ unsigned int cmd, uint64_t arg) {
  //   inode *inode = filp->f_dentry->d_inode;
  //   InodeInfo *fi = F2FS_I(inode);
  //   unsigned int flags;
  //   int ret;

  //   switch (cmd) {
  //   case FS_IOC_GETFLAGS:
  //     flags = fi->i_flags & FS_FL_USER_VISIBLE;
  //     return put_user(flags, (int __user *) arg);
  //   case FS_IOC_SETFLAGS:
  //   {
  //     unsigned int oldflags;

  //     ret = mnt_want_write(filp->f_path.mnt);
  //     if (ret)
  //       return ret;

  //     if (!inode_owner_or_capable(inode)) {
  //       ret = -EACCES;
  //       goto out;
  //     }

  //     if (get_user(flags, (int __user *) arg)) {
  //       ret = -EFAULT;
  //       goto out;
  //     }

  //     flags = f2fs_mask_flags(inode->i_mode, flags);

  //     mutex_lock(&inode->i_mutex_);

  //     oldflags = fi->i_flags;

  //     if ((flags ^ oldflags) & (FS_APPEND_FL | FS_IMMUTABLE_FL)) {
  //       if (!capable(CAP_LINUX_IMMUTABLE)) {
  //         mutex_unlock(&inode->i_mutex_);
  //         ret = -EPERM;
  //         goto out;
  //       }
  //     }

  //     flags = flags & FS_FL_USER_MODIFIABLE;
  //     flags |= oldflags & ~FS_FL_USER_MODIFIABLE;
  //     fi->i_flags = flags;
  //     mutex_unlock(&inode->i_mutex_);

  //     f2fs_set_inode_flags(inode);
  //     inode->i_ctime = CURRENT_TIME;
  //     MarkInodeDirty(inode);
  // out:
  //     mnt_drop_write(filp->f_path.mnt);
  //     return ret;
  //   }
  //   default:
  //     return -ENOTTY;
  //   }
// }
#endif

zx_status_t File::Read(void *data, size_t len, size_t off, size_t *out_actual) {
  uint64_t blk_start = off / kBlockSize;
  uint64_t blk_end = (off + len) / kBlockSize;
  size_t off_in_block = off % kBlockSize;
  size_t off_in_buf = 0;
  Page *data_page = nullptr;
  void *data_buf = nullptr;
  pgoff_t n;
  size_t left = len;
  uint64_t npages = (GetSize() + kBlockSize - 1) / kBlockSize;

  if (off >= GetSize()) {
    *out_actual = 0;
    return ZX_OK;
  }

  for (n = blk_start; n <= blk_end; n++) {
    bool is_empty_page = false;
    fs::SharedLock read_lock(io_lock_);
    if (zx_status_t ret = GetLockDataPage(n, &data_page); ret != ZX_OK) {
      if (ret == ZX_ERR_NOT_FOUND) {  // truncated page
        is_empty_page = true;
      } else {
        *out_actual = off_in_buf;
        return ret;
      }
    }

    size_t cur_len = std::min(static_cast<size_t>(kBlockSize - off_in_block), left);
    if (n == npages - 1) {
      if (GetSize() % kBlockSize > 0)
        cur_len = std::min(cur_len, static_cast<size_t>(GetSize() % kBlockSize));
    }

    if (is_empty_page) {
      memset(static_cast<char *>(data) + off_in_buf, 0, cur_len);
    } else {
      data_buf = PageAddress(data_page);
      memcpy(static_cast<char *>(data) + off_in_buf, static_cast<char *>(data_buf) + off_in_block,
             cur_len);
    }

    off_in_buf += cur_len;
    left -= cur_len;
    off_in_block = 0;

    if (!is_empty_page) {
      F2fsPutPage(data_page, 1);
      data_page = nullptr;
    }

    if (left == 0)
      break;

    if (n == npages - 1)
      break;
  }

  *out_actual = off_in_buf;

  return ZX_OK;
}

zx_status_t File::DoWrite(const void *data, size_t len, size_t offset, size_t *out_actual) {
  uint64_t blk_start = offset / kBlockSize;
  uint64_t blk_end = (offset + len) / kBlockSize;
  size_t off_in_block = offset % kBlockSize;
  size_t off_in_buf = 0;
  void *data_buf = nullptr;
  size_t left = len;

  if (len == 0)
    return ZX_OK;

  if (offset + len > static_cast<size_t>(Vfs()->MaxFileSize(Vfs()->RawSb().log_blocksize)))
    return ZX_ERR_INVALID_ARGS;

  std::vector<Page *> data_pages(blk_end - blk_start + 1, nullptr);

  for (uint64_t n = blk_start; n <= blk_end; n++) {
    uint64_t index = n - blk_start;
    size_t cur_len = std::min(static_cast<size_t>(kBlockSize - off_in_block), left);

    ZX_ASSERT(index < data_pages.size());

    if (zx_status_t ret = WriteBegin(n * kBlockSize + off_in_block, cur_len, &data_pages[index]);
        ret != ZX_OK) {
      // WriteBegin() tries to prepare in-memory buffers and direct node blocks for storing |data|
      // If it succeeds, data_pages[index] has a page of valid virtual memory.
      // If it fails with any reasons such as no space/memory,
      // every data_pages[less than index] is released, and DoWrite() returns with the err code.
      for (uint64_t m = 0; m < index; m++) {
        if (data_pages[m] != nullptr)
          F2fsPutPage(data_pages[m], 1);
      }

      *out_actual = 0;
      return ret;
    }

    off_in_block = 0;
    off_in_buf += cur_len;
    left -= cur_len;

    if (left == 0)
      break;
  }

  off_in_block = offset % kBlockSize;
  off_in_buf = 0;
  left = len;

  for (uint64_t n = blk_start; n <= blk_end; n++) {
    uint64_t index = n - blk_start;
    size_t cur_len = std::min(static_cast<size_t>(kBlockSize - off_in_block), left);

    ZX_ASSERT(index < data_pages.size());

    Page *data_page = data_pages[index];

    data_buf = PageAddress(data_page);
    memcpy(static_cast<char *>(data_buf) + off_in_block,
           static_cast<const char *>(data) + off_in_buf, cur_len);

    off_in_block = 0;
    off_in_buf += cur_len;
    left -= cur_len;

    std::lock_guard write_lock(io_lock_);
    SetSize(std::max(static_cast<size_t>(GetSize()), offset + off_in_buf));
#if 0  // porting needed
    // set_page_dirty(data_page, Vfs());
#else
    FlushDirtyDataPage(Vfs(), data_page);
#endif
    F2fsPutPage(data_page, 1);
    data_pages[index] = nullptr;

    if (left == 0)
      break;
  }

  if (off_in_buf > 0) {
    timespec cur_time;
    clock_gettime(CLOCK_REALTIME, &cur_time);
    SetCTime(cur_time);
    SetMTime(cur_time);
    MarkInodeDirty();
  }
  *out_actual = off_in_buf;

  return ZX_OK;
}

zx_status_t File::Write(const void *data, size_t len, size_t offset, size_t *out_actual) {
  return DoWrite(data, len, offset, out_actual);
}

zx_status_t File::Append(const void *data, size_t len, size_t *out_end, size_t *out_actual) {
  size_t off = GetSize();
  zx_status_t ret = DoWrite(data, len, off, out_actual);

  *out_end = off + *out_actual;

  return ret;
}

zx_status_t File::Truncate(size_t len) {
  if (len == GetSize())
    return ZX_OK;

  if (len > static_cast<size_t>(Vfs()->MaxFileSize(Vfs()->RawSb().log_blocksize)))
    return ZX_ERR_INVALID_ARGS;

  return DoTruncate(len);
}

}  // namespace f2fs
