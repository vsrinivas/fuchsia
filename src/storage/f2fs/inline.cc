// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dirent.h>

#include "src/storage/f2fs/f2fs.h"

namespace f2fs {

uint8_t *Dir::InlineDentryBitmap(Page *page) {
  Node *rn = page->GetAddress<Node>();
  Inode &ri = rn->i;
  return reinterpret_cast<uint8_t *>(
      &ri.i_addr[GetExtraISize() / sizeof(uint32_t) + kInlineStartOffset]);
}

uint64_t Dir::InlineDentryBitmapSize() const {
  return (MaxInlineDentry() + kBitsPerByte - 1) / kBitsPerByte;
}

DirEntry *Dir::InlineDentryArray(Page *page, VnodeF2fs &vnode) {
  uint8_t *base = InlineDentryBitmap(page);
  uint32_t reserved = safemath::checked_cast<uint32_t>(
      (vnode.MaxInlineData() -
       safemath::CheckMul(vnode.MaxInlineDentry(), (kSizeOfDirEntry + kDentrySlotLen)))
          .ValueOrDie());

  return reinterpret_cast<DirEntry *>(base + reserved);
}

uint8_t (*Dir::InlineDentryFilenameArray(Page *page, VnodeF2fs &vnode))[kDentrySlotLen] {
  uint8_t *base = InlineDentryBitmap(page);
  uint32_t reserved = safemath::checked_cast<uint32_t>(
      (vnode.MaxInlineData() - safemath::CheckMul(vnode.MaxInlineDentry(), kDentrySlotLen))
          .ValueOrDie());
  return reinterpret_cast<uint8_t(*)[kDentrySlotLen]>(base + reserved);
}

DirEntry *Dir::FindInInlineDir(std::string_view name, fbl::RefPtr<Page> *res_page) {
  LockedPage ipage;
  if (zx_status_t ret = fs()->GetNodeManager().GetNodePage(Ino(), &ipage); ret != ZX_OK)
    return nullptr;
  f2fs_hash_t namehash = DentryHash(name);

  for (uint32_t bit_pos = 0; bit_pos < MaxInlineDentry();) {
    bit_pos = FindNextBit(InlineDentryBitmap(ipage.get()), MaxInlineDentry(), bit_pos);
    if (bit_pos >= MaxInlineDentry()) {
      break;
    }

    DirEntry *de = &InlineDentryArray(ipage.get(), *this)[bit_pos];
    if (EarlyMatchName(name, namehash, *de)) {
      if (!memcmp(InlineDentryFilenameArray(ipage.get(), *this)[bit_pos], name.data(),
                  name.length())) {
        *res_page = ipage.release();

#ifdef __Fuchsia__
        if (de != nullptr) {
          fs()->GetDirEntryCache().UpdateDirEntry(Ino(), name, *de, kCachedInlineDirEntryPageIndex);
        }
#endif  // __Fuchsia__
        return de;
      }
    }

    // For the most part, it should be a bug when name_len is zero.
    // We stop here for figuring out where the bugs are occurred.
    ZX_DEBUG_ASSERT(de->name_len > 0);

    bit_pos += GetDentrySlots(LeToCpu(de->name_len));
  }

  return nullptr;
}

DirEntry *Dir::ParentInlineDir(fbl::RefPtr<Page> *out) {
  LockedPage ipage;
  if (zx_status_t ret = fs()->GetNodeManager().GetNodePage(Ino(), &ipage); ret != ZX_OK) {
    return nullptr;
  }

  DirEntry *de = &InlineDentryArray(ipage.get(), *this)[1];
  *out = ipage.release();
  return de;
}

zx_status_t Dir::MakeEmptyInlineDir(VnodeF2fs *vnode) {
  LockedPage ipage;

  if (zx_status_t err = fs()->GetNodeManager().GetNodePage(vnode->Ino(), &ipage); err != ZX_OK)
    return err;

  DirEntry *de = &InlineDentryArray(&(*ipage), *vnode)[0];
  de->name_len = CpuToLe(static_cast<uint16_t>(1));
  de->hash_code = 0;
  de->ino = CpuToLe(vnode->Ino());
  memcpy(InlineDentryFilenameArray(&(*ipage), *vnode)[0], ".", 1);
  SetDeType(de, vnode);

  de = &InlineDentryArray(&(*ipage), *vnode)[1];
  de->hash_code = 0;
  de->name_len = CpuToLe(static_cast<uint16_t>(2));
  de->ino = CpuToLe(Ino());
  memcpy(InlineDentryFilenameArray(&(*ipage), *vnode)[1], "..", 2);
  SetDeType(de, vnode);

  TestAndSetBit(0, InlineDentryBitmap(&(*ipage)));
  TestAndSetBit(1, InlineDentryBitmap(&(*ipage)));

  ipage->SetDirty();

  if (vnode->GetSize() < vnode->MaxInlineData()) {
    vnode->SetSize(vnode->MaxInlineData());
    vnode->SetFlag(InodeInfoFlag::kUpdateDir);
  }

  return ZX_OK;
}

unsigned int Dir::RoomInInlineDir(Page *ipage, int slots) {
  int bit_start = 0;

  while (true) {
    int zero_start = FindNextZeroBit(InlineDentryBitmap(ipage), MaxInlineDentry(), bit_start);
    if (zero_start >= static_cast<int>(MaxInlineDentry()))
      return MaxInlineDentry();

    int zero_end = FindNextBit(InlineDentryBitmap(ipage), MaxInlineDentry(), zero_start);
    if (zero_end - zero_start >= slots)
      return zero_start;

    bit_start = zero_end + 1;

    if (zero_end + 1 >= static_cast<int>(MaxInlineDentry())) {
      return MaxInlineDentry();
    }
  }
}

zx_status_t Dir::ConvertInlineDir() {
  LockedPage page;
  if (zx_status_t ret = GrabCachePage(0, &page); ret != ZX_OK) {
    return ret;
  }

  LockedPage dnode_page;
  if (zx_status_t err = fs()->GetNodeManager().GetLockedDnodePage(*this, 0, &dnode_page);
      err != ZX_OK) {
    return err;
  }

  uint32_t ofs_in_dnode;
  if (auto result = fs()->GetNodeManager().GetOfsInDnode(*this, 0); result.is_error()) {
    return result.error_value();
  } else {
    ofs_in_dnode = result.value();
  }

  NodePage *ipage = &dnode_page.GetPage<NodePage>();
  block_t data_blkaddr = DatablockAddr(ipage, ofs_in_dnode);
  ZX_DEBUG_ASSERT(data_blkaddr == kNullAddr);

  if (zx_status_t err = ReserveNewBlock(*ipage, ofs_in_dnode); err != ZX_OK) {
    return err;
  }

  page->WaitOnWriteback();
  page->ZeroUserSegment(0, kPageSize);

  DentryBlock *dentry_blk = page->GetAddress<DentryBlock>();

  // copy data from inline dentry block to new dentry block
  memcpy(dentry_blk->dentry_bitmap, InlineDentryBitmap(ipage), InlineDentryBitmapSize());
  memcpy(dentry_blk->dentry, InlineDentryArray(ipage, *this), sizeof(DirEntry) * MaxInlineDentry());
  memcpy(dentry_blk->filename, InlineDentryFilenameArray(ipage, *this),
         MaxInlineDentry() * kNameLen);

  page->SetDirty();
  // clear inline dir and flag after data writeback
  ipage->WaitOnWriteback();
  ipage->ZeroUserSegment(InlineDataOffset(), InlineDataOffset() + MaxInlineData());
  ClearFlag(InodeInfoFlag::kInlineDentry);

  if (!TestFlag(InodeInfoFlag::kInlineXattr)) {
    SetInlineXattrAddrs(0);
  }

  if (GetSize() < kPageSize) {
    SetSize(kPageSize);
    SetFlag(InodeInfoFlag::kUpdateDir);
  }

  UpdateInode(ipage);
#if 0  // porting needed
  // stat_dec_inline_inode(dir);
#endif
  return ZX_OK;
}

zx::result<bool> Dir::AddInlineEntry(std::string_view name, VnodeF2fs *vnode) {
  {
    LockedPage ipage;
    if (zx_status_t err = fs()->GetNodeManager().GetNodePage(Ino(), &ipage); err != ZX_OK) {
      return zx::error(err);
    }

    f2fs_hash_t name_hash = DentryHash(name);
    int slots = GetDentrySlots(static_cast<uint16_t>(name.length()));
    unsigned int bit_pos = RoomInInlineDir(ipage.get(), slots);
    if (bit_pos < MaxInlineDentry()) {
      ipage->WaitOnWriteback();

      if (zx_status_t err = InitInodeMetadata(vnode); err != ZX_OK) {
        if (ClearFlag(InodeInfoFlag::kUpdateDir)) {
          UpdateInode(ipage.get());
        }
        return zx::error(err);
      }

      DirEntry *de = &InlineDentryArray(ipage.get(), *this)[bit_pos];
      de->hash_code = name_hash;
      de->name_len = static_cast<uint16_t>(CpuToLe(name.length()));
      memcpy(InlineDentryFilenameArray(ipage.get(), *this)[bit_pos], name.data(), name.length());
      de->ino = CpuToLe(vnode->Ino());
      SetDeType(de, vnode);
      for (int i = 0; i < slots; ++i) {
        TestAndSetBit(bit_pos + i, InlineDentryBitmap(ipage.get()));
      }

#ifdef __Fuchsia__
      if (de != nullptr) {
        fs()->GetDirEntryCache().UpdateDirEntry(Ino(), name, *de, kCachedInlineDirEntryPageIndex);
      }
#endif  // __Fuchsia__

      ipage->SetDirty();
      UpdateParentMetadata(vnode, 0);
      vnode->WriteInode();
      UpdateInode(ipage.get());

      ClearFlag(InodeInfoFlag::kUpdateDir);
      return zx::ok(false);
    }
  }

  if (auto ret = ConvertInlineDir(); ret != ZX_OK) {
    return zx::error(ret);
  }
  return zx::ok(true);
}

void Dir::DeleteInlineEntry(DirEntry *dentry, fbl::RefPtr<Page> &page, VnodeF2fs *vnode) {
  LockedPage lock_page(page);
  page->WaitOnWriteback();

  unsigned int bit_pos = static_cast<uint32_t>(dentry - InlineDentryArray(page.get(), *this));
  int slots = GetDentrySlots(LeToCpu(dentry->name_len));
  for (int i = 0; i < slots; ++i) {
    TestAndClearBit(bit_pos + i, InlineDentryBitmap(page.get()));
  }

  page->SetDirty();

#ifdef __Fuchsia__
  std::string_view remove_name(
      reinterpret_cast<char *>(InlineDentryFilenameArray(page.get(), *this)[bit_pos]),
      LeToCpu(dentry->name_len));

  fs()->GetDirEntryCache().RemoveDirEntry(Ino(), remove_name);
#endif  // __Fuchsia__

  timespec cur_time;
  clock_gettime(CLOCK_REALTIME, &cur_time);
  SetCTime(cur_time);
  SetMTime(cur_time);

  if (vnode && vnode->IsDir()) {
    DropNlink();
  }

  if (vnode) {
    clock_gettime(CLOCK_REALTIME, &cur_time);
    SetCTime(cur_time);
    SetMTime(cur_time);
    vnode->SetCTime(cur_time);
    vnode->DropNlink();
    if (vnode->IsDir()) {
      vnode->DropNlink();
      vnode->InitSize();
    }
    vnode->WriteInode(false);
    if (vnode->GetNlink() == 0) {
      fs()->AddOrphanInode(vnode);
    }
  }
  UpdateInode(page.get());
}

bool Dir::IsEmptyInlineDir() {
  LockedPage ipage;

  if (zx_status_t err = fs()->GetNodeManager().GetNodePage(Ino(), &ipage); err != ZX_OK)
    return false;

  unsigned int bit_pos = 2;
  bit_pos = FindNextBit(InlineDentryBitmap(ipage.get()), MaxInlineDentry(), bit_pos);

  if (bit_pos < MaxInlineDentry()) {
    return false;
  }

  return true;
}

zx_status_t Dir::ReadInlineDir(fs::VdirCookie *cookie, void *dirents, size_t len,
                               size_t *out_actual) {
  fs::DirentFiller df(dirents, len);
  uint64_t *pos_cookie = reinterpret_cast<uint64_t *>(cookie);

  if (*pos_cookie == MaxInlineDentry()) {
    *out_actual = 0;
    return ZX_OK;
  }

  LockedPage ipage;

  if (zx_status_t err = fs()->GetNodeManager().GetNodePage(Ino(), &ipage); err != ZX_OK)
    return err;

  const unsigned char *types = kFiletypeTable;
  uint32_t bit_pos = *pos_cookie % MaxInlineDentry();

  while (bit_pos < MaxInlineDentry()) {
    bit_pos = FindNextBit(InlineDentryBitmap(ipage.get()), MaxInlineDentry(), bit_pos);
    if (bit_pos >= MaxInlineDentry()) {
      break;
    }

    DirEntry *de = &InlineDentryArray(ipage.get(), *this)[bit_pos];
    unsigned char d_type = DT_UNKNOWN;
    if (de->file_type < static_cast<uint8_t>(FileType::kFtMax))
      d_type = types[de->file_type];

    std::string_view name(
        reinterpret_cast<char *>(InlineDentryFilenameArray(ipage.get(), *this)[bit_pos]),
        LeToCpu(de->name_len));

    if (de->ino && name != "..") {
      if (zx_status_t ret = df.Next(name, d_type, LeToCpu(de->ino)); ret != ZX_OK) {
        *pos_cookie = bit_pos;

        *out_actual = df.BytesFilled();
        return ZX_OK;
      }
    }

    bit_pos += GetDentrySlots(LeToCpu(de->name_len));
  }

  *pos_cookie = MaxInlineDentry();
  *out_actual = df.BytesFilled();

  return ZX_OK;
}

uint8_t *File::InlineDataPtr(Page *page) {
  Node *rn = page->GetAddress<Node>();
  Inode &ri = rn->i;
  return reinterpret_cast<uint8_t *>(
      &ri.i_addr[GetExtraISize() / sizeof(uint32_t) + kInlineStartOffset]);
}

#ifdef __Fuchsia__
zx::result<> File::PopulateVmoWithInlineData(zx::vmo &vmo) {
  LockedPage inline_page;
  if (zx_status_t ret = fs()->GetNodeManager().GetNodePage(Ino(), &inline_page); ret != ZX_OK) {
    return zx::error(ret);
  }
  // Fill |vmo| only when it has valid inline data.
  if (TestFlag(InodeInfoFlag::kDataExist)) {
    uint8_t *inline_data = InlineDataPtr(inline_page.get());
    size_t size = GetSize();
    if (size) {
      vmo.write(inline_data, 0, size);
    }
  }
  inline_page->SetMmapped();
  return zx::ok();
}
#endif  // __Fuchsia__

zx_status_t File::ReadInline(void *data, size_t len, size_t off, size_t *out_actual) {
  LockedPage inline_page;
  if (zx_status_t ret = fs()->GetNodeManager().GetNodePage(Ino(), &inline_page); ret != ZX_OK) {
    return ret;
  }

  uint8_t *inline_data = InlineDataPtr(inline_page.get());
  size_t cur_len = std::min(len, GetSize() - off);
  memcpy(static_cast<uint8_t *>(data), inline_data + off, cur_len);

  *out_actual = cur_len;

  return ZX_OK;
}

zx_status_t File::ConvertInlineData() {
  LockedPage page;
  if (zx_status_t ret = GrabCachePage(0, &page); ret != ZX_OK) {
    return ret;
  }

  LockedPage dnode_page;
  if (zx_status_t err = fs()->GetNodeManager().GetLockedDnodePage(*this, 0, &dnode_page);
      err != ZX_OK) {
    return err;
  }

  uint32_t ofs_in_dnode;
  if (auto result = fs()->GetNodeManager().GetOfsInDnode(*this, 0); result.is_error()) {
    return result.error_value();
  } else {
    ofs_in_dnode = result.value();
  }

  NodePage *ipage = &dnode_page.GetPage<NodePage>();
  block_t data_blkaddr = DatablockAddr(ipage, ofs_in_dnode);
  ZX_DEBUG_ASSERT(data_blkaddr == kNullAddr);

  if (zx_status_t err = ReserveNewBlock(*ipage, ofs_in_dnode); err != ZX_OK) {
    return err;
  }

  page->WaitOnWriteback();
  page->ZeroUserSegment(0, kPageSize);

  uint8_t *inline_data = InlineDataPtr(ipage);
  memcpy(page->GetAddress(), inline_data, GetSize());

  page->SetDirty();

  ipage->WaitOnWriteback();
  ipage->ZeroUserSegment(InlineDataOffset(), InlineDataOffset() + MaxInlineData());
  // Clear regarding flags since we moved inline data to a data Page.
  ipage->ClearMmapped();
  ClearFlag(InodeInfoFlag::kInlineData);
  ClearFlag(InodeInfoFlag::kDataExist);

  UpdateInode(ipage);

  return ZX_OK;
}

zx_status_t File::WriteInline(const void *data, size_t len, size_t offset, size_t *out_actual) {
  LockedPage inline_page;
  if (zx_status_t ret = fs()->GetNodeManager().GetNodePage(Ino(), &inline_page); ret != ZX_OK) {
    return ret;
  }

  inline_page->WaitOnWriteback();

  uint8_t *inline_data = InlineDataPtr(inline_page.get());
  memcpy(inline_data + offset, static_cast<const uint8_t *>(data), len);
  if (inline_page->IsMapped()) {
    // Apply changes to its paged VMO.
    ZX_ASSERT(WritePagedVmo(inline_data + offset, offset, len) == ZX_OK);
  }

  SetSize(std::max(static_cast<size_t>(GetSize()), offset + len));
  SetFlag(InodeInfoFlag::kDataExist);
  inline_page->SetDirty();

  timespec cur_time;
  clock_gettime(CLOCK_REALTIME, &cur_time);
  SetCTime(cur_time);
  SetMTime(cur_time);
  MarkInodeDirty();

  *out_actual = len;

  return ZX_OK;
}

zx_status_t File::TruncateInline(size_t len, bool is_recover) {
  {
    LockedPage inline_page;
    if (zx_status_t ret = fs()->GetNodeManager().GetNodePage(Ino(), &inline_page); ret != ZX_OK) {
      return ret;
    }

    inline_page->WaitOnWriteback();

    uint8_t *inline_data = InlineDataPtr(inline_page.get());
    size_t size_diff = (len > GetSize()) ? (len - GetSize()) : (GetSize() - len);
    size_t offset = ((len > GetSize()) ? GetSize() : len);
    memset(inline_data + offset, 0, size_diff);
    if (inline_page->IsMapped()) {
      // Apply changes to its paged VMO.
      ZX_ASSERT(WritePagedVmo(inline_data + offset, offset, size_diff) == ZX_OK);
    }

    // When removing inline data during recovery, file size should not be modified.
    if (!is_recover) {
      SetSize(len);
    }
    if (len == 0) {
      ClearFlag(InodeInfoFlag::kDataExist);
    }

    inline_page->SetDirty();
  }
  timespec cur_time;
  clock_gettime(CLOCK_REALTIME, &cur_time);
  SetCTime(cur_time);
  SetMTime(cur_time);
  MarkInodeDirty();

  return ZX_OK;
}

zx_status_t File::RecoverInlineData(NodePage &page) {
  // The inline_data recovery policy is as follows.
  // [prev.] [next] of inline_data flag
  //    o       o  -> recover inline_data
  //    o       x  -> remove inline_data, and then recover data blocks
  //    x       o  -> remove data blocks, and then recover inline_data (TODO)
  //    x       x  -> recover data blocks
  // ([prev.] is checkpointed data. And [next] is data written and fsynced after checkpoint.)

  Inode *raw_inode = nullptr;
  if (IsInode(page)) {
    raw_inode = &page.GetAddress<Node>()->i;
  }

  // [next] have inline data.
  if (raw_inode && (raw_inode->i_inline & kInlineData)) {
    // TODO: We should consider converting data blocks to inline data.

    // Process inline.
    LockedPage ipage;
    if (zx_status_t err = fs()->GetNodeManager().GetNodePage(Ino(), &ipage); err != ZX_OK) {
      return err;
    }
    ipage->WaitOnWriteback();
    memcpy(InlineDataPtr(ipage.get()), InlineDataPtr(&page), MaxInlineData());

    SetFlag(InodeInfoFlag::kInlineData);
    SetFlag(InodeInfoFlag::kDataExist);

    ipage->SetDirty();
    return ZX_OK;
  }

  // [prev.] has inline data but [next] has no inline data.
  if (TestFlag(InodeInfoFlag::kInlineData)) {
    TruncateInline(0, true);
    ClearFlag(InodeInfoFlag::kInlineData);
    ClearFlag(InodeInfoFlag::kDataExist);
  }
  return ZX_ERR_NOT_SUPPORTED;
}

}  // namespace f2fs
