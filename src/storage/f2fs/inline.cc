// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "f2fs.h"

namespace f2fs {
namespace {

inline InlineDentry *GetInlineDentryAddr(Page *page) {
  Node *rn = static_cast<Node *>(PageAddress(page));
  Inode &ri = rn->i;
  return reinterpret_cast<InlineDentry *>(&(ri.i_addr[1]));
}

}  // namespace

DirEntry *Dir::FindInInlineDir(const std::string_view &name, Page **res_page) {
  Page *node_page = nullptr;

  if (zx_status_t ret = Vfs()->Nodemgr().GetNodePage(Ino(), &node_page); ret != ZX_OK)
    return nullptr;

  f2fs_hash_t namehash = DentryHash(name.data(), name.length());

  InlineDentry *dentry_blk = GetInlineDentryAddr(node_page);

  DirEntry *de = nullptr;

  for (uint64_t bit_pos = 0; bit_pos < kNrInlineDentry;) {
    bit_pos = find_next_bit_le(&dentry_blk->dentry_bitmap, kNrInlineDentry, bit_pos);
    if (bit_pos >= kNrInlineDentry)
      break;

    de = &dentry_blk->dentry[bit_pos];
    if (EarlyMatchName(name.data(), name.length(), namehash, de)) {
      if (!memcmp(dentry_blk->filename[bit_pos], name.data(), name.length())) {
        *res_page = node_page;
#if 0  // porting needed
       // unlock_page(node_page);
#endif
        return de;
      }
    }

    // For the most part, it should be a bug when name_len is zero.
    // We stop here for figuring out where the bugs are occurred.
#if 0  // porting needed
    // f2fs_bug_on(F2FS_P_SB(node_page), !de->name_len);
#else
    ZX_ASSERT(de->name_len > 0);
#endif

    bit_pos += GetDentrySlots(LeToCpu(de->name_len));
  }

  de = nullptr;
#if 0  // porting needed
  // unlock_page(node_page);
#endif
  return de;
}

DirEntry *Dir::ParentInlineDir(Page **p) {
  Page *node_page = nullptr;

  if (zx_status_t ret = Vfs()->Nodemgr().GetNodePage(Ino(), &node_page); ret != ZX_OK)
    return nullptr;

  InlineDentry *dentry_blk = GetInlineDentryAddr(node_page);
  DirEntry *de = &dentry_blk->dentry[1];
  *p = node_page;
#if 0  // porting needed
  unlock_page(node_page);
#endif
  return de;
}

zx_status_t Dir::MakeEmptyInlineDir(VnodeF2fs *vnode, VnodeF2fs *parent) {
  Page *ipage = nullptr;

  if (zx_status_t err = Vfs()->Nodemgr().GetNodePage(vnode->Ino(), &ipage); err != ZX_OK)
    return err;

  InlineDentry *dentry_blk = GetInlineDentryAddr(ipage);

  DirEntry *de = &dentry_blk->dentry[0];
  de->name_len = CpuToLe(static_cast<uint16_t>(1));
  de->hash_code = 0;
  de->ino = CpuToLe(vnode->Ino());
  memcpy(dentry_blk->filename[0], ".", 1);
  SetDeType(de, vnode);

  de = &dentry_blk->dentry[1];
  de->hash_code = 0;
  de->name_len = CpuToLe(static_cast<uint16_t>(2));
  de->ino = CpuToLe(parent->Ino());
  memcpy(dentry_blk->filename[1], "..", 2);
  SetDeType(de, vnode);

  test_and_set_bit_le(0, &dentry_blk->dentry_bitmap);
  test_and_set_bit_le(1, &dentry_blk->dentry_bitmap);

#if 0  // porting needed
  // set_page_dirty(ipage);
#else
  FlushDirtyNodePage(Vfs(), ipage);
#endif

  if (vnode->GetSize() < kMaxInlineData) {
    vnode->SetSize(kMaxInlineData);
    vnode->SetFlag(InodeInfoFlag::kUpdateDir);
  }

  F2fsPutPage(ipage, 1);

  return ZX_OK;
}

unsigned int Dir::RoomInInlineDir(InlineDentry *dentry_blk, int slots) {
  int bit_start = 0;

  while (true) {
    int zero_start = find_next_zero_bit_le(&dentry_blk->dentry_bitmap, kNrInlineDentry, bit_start);
    if (zero_start >= static_cast<int>(kNrInlineDentry))
      return kNrInlineDentry;

    int zero_end = find_next_bit_le(&dentry_blk->dentry_bitmap, kNrInlineDentry, zero_start);
    if (zero_end - zero_start >= slots)
      return zero_start;

    bit_start = zero_end + 1;

    if (zero_end + 1 >= static_cast<int>(kNrInlineDentry))
      return kNrInlineDentry;
  }
}

zx_status_t Dir::ConvertInlineDir(InlineDentry *inline_dentry) {
  Page *page;
  if (page = GrabCachePage(this, Ino(), 0); page == nullptr)
    return ZX_ERR_NO_MEMORY;

  DnodeOfData dn;
  SetNewDnode(&dn, this, nullptr, nullptr, 0);
  if (zx_status_t err = Vfs()->Nodemgr().GetDnodeOfData(&dn, 0, 0); err != ZX_OK)
    return err;

  if (dn.data_blkaddr == kNullAddr) {
    if (zx_status_t err = ReserveNewBlock(&dn); err != ZX_OK) {
      F2fsPutPage(page, 1);
      F2fsPutDnode(&dn);
      return err;
    }
  }

  F2fsPutDnode(&dn);

  WaitOnPageWriteback(page);
  ZeroUserSegment(page, 0, kPageCacheSize);

  DentryBlock *dentry_blk = static_cast<DentryBlock *>(PageAddress(page));

  /* copy data from inline dentry block to new dentry block */
  memcpy(dentry_blk->dentry_bitmap, inline_dentry->dentry_bitmap, kInlineDentryBitmapSize);
  memcpy(dentry_blk->dentry, inline_dentry->dentry, sizeof(DirEntry) * kNrInlineDentry);
  memcpy(dentry_blk->filename, inline_dentry->filename, kNrInlineDentry * kNameLen);

  // TODO: inode update after data page flush
  // Since there is no pager, inode information in UpdateInode and FlushDirtyDataPage are not
  // synchronized. So, inode update should run first, then FlushDirtyDataPage will use correct inode
  // information. When pager is available, inode update can run after setting data page dirty
  Page *ipage = nullptr;
  if (zx_status_t err = Vfs()->Nodemgr().GetNodePage(Ino(), &ipage); err != ZX_OK)
    return err;

  // clear inline dir and flag after data writeback
  ZeroUserSegment(ipage, kInlineDataOffset, kInlineDataOffset + kMaxInlineData);
  ClearFlag(InodeInfoFlag::kInlineDentry);

  if (GetSize() < kPageCacheSize) {
    SetSize(kPageCacheSize);
    SetFlag(InodeInfoFlag::kUpdateDir);
  }

  UpdateInode(ipage);
  F2fsPutPage(ipage, 1);

#if 0  // porting needed
//   kunmap(page);
#endif
  SetPageUptodate(page);
#if 0  // porting needed
    // set_page_dirty(page);
#else
  FlushDirtyDataPage(Vfs(), page);
#endif

#if 0  // porting needed
  // stat_dec_inline_inode(dir);
#endif

  F2fsPutPage(page, 1);

  return ZX_OK;
}

zx_status_t Dir::AddInlineEntry(std::string_view name, VnodeF2fs *vnode, bool *is_converted) {
  *is_converted = false;

  f2fs_hash_t name_hash = DentryHash(name.data(), name.length());

  Page *ipage = nullptr;
  if (zx_status_t err = Vfs()->Nodemgr().GetNodePage(Ino(), &ipage); err != ZX_OK)
    return err;

  InlineDentry *dentry_blk = GetInlineDentryAddr(ipage);

  int slots = GetDentrySlots(name.length());
  unsigned int bit_pos = RoomInInlineDir(dentry_blk, slots);
  if (bit_pos >= kNrInlineDentry) {
    ZX_ASSERT(ConvertInlineDir(dentry_blk) == ZX_OK);

    *is_converted = true;
    F2fsPutPage(ipage, 1);
    return ZX_OK;
  }

  WaitOnPageWriteback(ipage);

#if 0  // porting needed
  // down_write(&F2FS_I(inode)->i_sem);
#endif

  if (zx_status_t err = InitInodeMetadata(vnode); err != ZX_OK) {
#if 0  // porting needed
    // up_write(&F2FS_I(inode)->i_sem);
#endif

    if (TestFlag(InodeInfoFlag::kUpdateDir)) {
      UpdateInode(ipage);
      ClearFlag(InodeInfoFlag::kUpdateDir);
    }
    F2fsPutPage(ipage, 1);
    return err;
  }

  DirEntry *de = &dentry_blk->dentry[bit_pos];
  de->hash_code = name_hash;
  de->name_len = CpuToLe(name.length());
  memcpy(dentry_blk->filename[bit_pos], name.data(), name.length());
  de->ino = CpuToLe(vnode->Ino());
  SetDeType(de, vnode);
  for (int i = 0; i < slots; i++)
    test_and_set_bit_le(bit_pos + i, &dentry_blk->dentry_bitmap);
#if 0  // porting needed
  // set_page_dirty(ipage);
#else
  FlushDirtyNodePage(Vfs(), ipage);
#endif

  vnode->SetParentNid(Ino());
  vnode->UpdateInode(ipage);
  UpdateParentMetadata(vnode, 0);

#if 0  // porting needed
  // up_write(&F2FS_I(inode)->i_sem);
#endif

  if (TestFlag(InodeInfoFlag::kUpdateDir)) {
    WriteInode(nullptr);
    ClearFlag(InodeInfoFlag::kUpdateDir);
  }

  F2fsPutPage(ipage, 1);
  return ZX_OK;
}

void Dir::DeleteInlineEntry(DirEntry *dentry, Page *page, VnodeF2fs *vnode) {
#if 0  // porting needed
  // lock_page(page);
#endif
  WaitOnPageWriteback(page);

  InlineDentry *inline_dentry = GetInlineDentryAddr(page);

  unsigned int bit_pos = dentry - inline_dentry->dentry;
  int slots = GetDentrySlots(LeToCpu(dentry->name_len));
  for (int i = 0; i < slots; i++)
    test_and_clear_bit_le(bit_pos + i, &inline_dentry->dentry_bitmap);

#if 0  // porting needed
  // set_page_dirty(page);
#else
  FlushDirtyNodePage(Vfs(), page);
#endif

  if (vnode) {
    timespec cur_time;
    clock_gettime(CLOCK_REALTIME, &cur_time);
    SetCTime(cur_time);
    SetMTime(cur_time);
    vnode->SetCTime(cur_time);
    vnode->DropNlink();
    if (vnode->IsDir()) {
      vnode->DropNlink();
      vnode->SetSize(0);
    }
    vnode->WriteInode(NULL);
    if (vnode->GetNlink() == 0)
      Vfs()->AddOrphanInode(vnode);
  }

  F2fsPutPage(page, 1);
}

bool Dir::IsEmptyInlineDir() {
  Page *ipage = nullptr;

  if (zx_status_t err = Vfs()->Nodemgr().GetNodePage(Ino(), &ipage); err != ZX_OK)
    return false;

  InlineDentry *dentry_blk = GetInlineDentryAddr(ipage);
  unsigned int bit_pos = 2;
  bit_pos = find_next_bit_le(&dentry_blk->dentry_bitmap, kNrInlineDentry, bit_pos);

  F2fsPutPage(ipage, 1);

  if (bit_pos < kNrInlineDentry)
    return false;

  return true;
}

zx_status_t Dir::ReadInlineDir(fs::VdirCookie *cookie, void *dirents, size_t len,
                               size_t *out_actual) {
  fs::DirentFiller df(dirents, len);
  uint64_t *pos_cookie = reinterpret_cast<uint64_t *>(cookie);

  if (*pos_cookie == kNrInlineDentry) {
    *out_actual = 0;
    return ZX_OK;
  }

  Page *ipage = nullptr;

  if (zx_status_t err = Vfs()->Nodemgr().GetNodePage(Ino(), &ipage); err != ZX_OK)
    return err;

  const unsigned char *types = kFiletypeTable;

  unsigned int bit_pos = *pos_cookie % kNrInlineDentry;
  InlineDentry *inline_dentry = GetInlineDentryAddr(ipage);

  while (bit_pos < kNrInlineDentry) {
    bit_pos = find_next_bit_le(&inline_dentry->dentry_bitmap, kNrInlineDentry, bit_pos);
    if (bit_pos >= kNrInlineDentry)
      break;

    DirEntry *de = &inline_dentry->dentry[bit_pos];
    unsigned char d_type = DT_UNKNOWN;
    if (de->file_type < static_cast<uint8_t>(FileType::kFtMax))
      d_type = types[de->file_type];

    std::string_view name(reinterpret_cast<char *>(inline_dentry->filename[bit_pos]),
                          LeToCpu(de->name_len));

    if (de->ino && name != "..") {
      if (zx_status_t ret = df.Next(name, d_type, LeToCpu(de->ino)); ret != ZX_OK) {
        *pos_cookie = bit_pos;
        F2fsPutPage(ipage, 1);

        *out_actual = df.BytesFilled();
        return ZX_OK;
      }
    }

    bit_pos += GetDentrySlots(LeToCpu(de->name_len));
    *pos_cookie = bit_pos;
  }

  *pos_cookie = kNrInlineDentry;
  F2fsPutPage(ipage, 1);

  *out_actual = df.BytesFilled();

  return ZX_OK;
}

}  // namespace f2fs
