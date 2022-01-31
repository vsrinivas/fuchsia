// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dirent.h>
#include <sys/stat.h>

#include <safemath/checked_math.h>

#include "src/storage/f2fs/f2fs.h"

namespace f2fs {

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wc99-designator"
const unsigned char kFiletypeTable[static_cast<uint8_t>(FileType::kFtMax)] = {
    [static_cast<uint8_t>(FileType::kFtUnknown)] = DT_UNKNOWN,
    [static_cast<uint8_t>(FileType::kFtRegFile)] = DT_REG,
    [static_cast<uint8_t>(FileType::kFtDir)] = DT_DIR,
    [static_cast<uint8_t>(FileType::kFtChrdev)] = DT_CHR,
    [static_cast<uint8_t>(FileType::kFtBlkdev)] = DT_BLK,
    [static_cast<uint8_t>(FileType::kFtFifo)] = DT_FIFO,
    [static_cast<uint8_t>(FileType::kFtSock)] = DT_SOCK,
    [static_cast<uint8_t>(FileType::kFtSymlink)] = DT_LNK,
};

constexpr unsigned int kStatShift = 12;

const unsigned char kTypeByMode[S_IFMT >> kStatShift] = {
    [S_IFREG >> kStatShift] = static_cast<uint8_t>(FileType::kFtRegFile),
    [S_IFDIR >> kStatShift] = static_cast<uint8_t>(FileType::kFtDir),
    [S_IFCHR >> kStatShift] = static_cast<uint8_t>(FileType::kFtChrdev),
    [S_IFBLK >> kStatShift] = static_cast<uint8_t>(FileType::kFtBlkdev),
    [S_IFIFO >> kStatShift] = static_cast<uint8_t>(FileType::kFtFifo),
    [S_IFSOCK >> kStatShift] = static_cast<uint8_t>(FileType::kFtSock),
    [S_IFLNK >> kStatShift] = static_cast<uint8_t>(FileType::kFtSymlink),
};
#pragma GCC diagnostic pop

Dir::Dir(F2fs *fs, ino_t ino) : VnodeF2fs(fs, ino) {}

block_t Dir::DirBlocks() { return safemath::checked_cast<block_t>(GetBlockCount()); }

uint32_t Dir::DirBuckets(uint32_t level, uint8_t dir_level) {
  if (level + dir_level < kMaxDirHashDepth / 2)
    return 1 << (level + dir_level);
  else
    return 1 << ((kMaxDirHashDepth / 2) - 1);
}

uint32_t Dir::BucketBlocks(uint32_t level) {
  if (level < kMaxDirHashDepth / 2)
    return 2;
  else
    return 4;
}

void Dir::SetDeType(DirEntry *de, VnodeF2fs *vnode) {
  de->file_type = kTypeByMode[(vnode->GetMode() & S_IFMT) >> kStatShift];
}

uint64_t Dir::DirBlockIndex(uint32_t level, uint8_t dir_level, uint32_t idx) {
  uint64_t bidx = 0;

  for (uint32_t i = 0; i < level; ++i) {
    bidx += DirBuckets(i, dir_level) * BucketBlocks(i);
  }
  bidx += idx * BucketBlocks(level);
  return bidx;
}

bool Dir::EarlyMatchName(std::string_view name, f2fs_hash_t namehash, const DirEntry &de) {
  if (LeToCpu(de.name_len) != name.length())
    return false;

  if (LeToCpu(de.hash_code) != namehash)
    return false;

  return true;
}

DirEntry *Dir::FindInBlock(fbl::RefPtr<Page> dentry_page, std::string_view name,
                           uint64_t *max_slots, f2fs_hash_t namehash, fbl::RefPtr<Page> *res_page) {
  DirEntry *de;
  uint32_t bit_pos, end_pos, next_pos;
  DentryBlock *dentry_blk = static_cast<DentryBlock *>(dentry_page->GetAddress());
  int slots;

  bit_pos = FindNextBit(dentry_blk->dentry_bitmap, kNrDentryInBlock, 0);
  while (bit_pos < kNrDentryInBlock) {
    de = &dentry_blk->dentry[bit_pos];
    slots = (LeToCpu(de->name_len) + kNameLen - 1) / kNameLen;

    if (EarlyMatchName(name, namehash, *de)) {
      if (!memcmp(dentry_blk->filename[bit_pos], name.data(), name.length())) {
        *res_page = std::move(dentry_page);
        return de;
      }
    }
    next_pos = bit_pos + slots;
    bit_pos = FindNextBit(dentry_blk->dentry_bitmap, kNrDentryInBlock, next_pos);
    if (bit_pos >= kNrDentryInBlock)
      end_pos = kNrDentryInBlock;
    else
      end_pos = bit_pos;
    if (*max_slots < end_pos - next_pos)
      *max_slots = end_pos - next_pos;
  }

  de = nullptr;
  return de;
}

DirEntry *Dir::FindInLevel(unsigned int level, std::string_view name, f2fs_hash_t namehash,
                           fbl::RefPtr<Page> *res_page) {
  uint64_t slot = (name.length() + kNameLen - 1) / kNameLen;
  unsigned int nbucket, nblock;
  uint64_t bidx, end_block;
  DirEntry *de = nullptr;
  bool room = false;
  uint64_t max_slots = 0;

  ZX_ASSERT(level <= kMaxDirHashDepth);

  nbucket = DirBuckets(level, GetDirLevel());
  nblock = BucketBlocks(level);

  bidx = DirBlockIndex(level, GetDirLevel(), namehash % nbucket);
  end_block = bidx + nblock;

  for (; bidx < end_block; ++bidx) {
    fbl::RefPtr<Page> dentry_page;
    // no need to allocate new dentry pages to all the indices
    if (FindDataPage(bidx, &dentry_page) != ZX_OK) {
      room = true;
      continue;
    }
    if (de = FindInBlock(dentry_page, name, &max_slots, namehash, res_page); de != nullptr) {
      break;
    }
    if (max_slots >= slot) {
      room = true;
    }
    Page::PutPage(std::move(dentry_page), false);
  }

  if (!de && room && !IsSameDirHash(namehash)) {
    SetDirHash(namehash, level);
  }

  return de;
}

// Find an entry in the specified directory with the wanted name.
// It returns the page where the entry was found (as a parameter - res_page),
// and the entry itself. Page is returned mapped and unlocked.
// Entry is guaranteed to be valid.
DirEntry *Dir::FindEntryOnDevice(std::string_view name, fbl::RefPtr<Page> *res_page) {
  uint64_t npages = DirBlocks();
  DirEntry *de = nullptr;
  f2fs_hash_t name_hash;
  unsigned int max_depth;
  unsigned int level;

  if (TestFlag(InodeInfoFlag::kInlineDentry)) {
    return FindInInlineDir(name, res_page);
  }

  if (npages == 0)
    return nullptr;

  *res_page = nullptr;

  name_hash = DentryHash(name);
  max_depth = static_cast<unsigned int>(GetCurDirDepth());

  for (level = 0; level < max_depth; ++level) {
    if (de = FindInLevel(level, name, name_hash, res_page); de != nullptr)
      break;
  }
  if (!de && !IsSameDirHash(name_hash)) {
    SetDirHash(name_hash, level - 1);
  }

#ifdef __Fuchsia__
  if (de != nullptr) {
    Vfs()->GetDirEntryCache().UpdateDirEntry(Ino(), name, *de, (*res_page)->GetIndex());
  }
#endif  // __Fuchsia__

  return de;
}

DirEntry *Dir::FindEntry(std::string_view name, fbl::RefPtr<Page> *res_page) {
#ifdef __Fuchsia__
  if (auto cache_page_index = Vfs()->GetDirEntryCache().LookupDataPageIndex(Ino(), name);
      !cache_page_index.is_error()) {
    if (TestFlag(InodeInfoFlag::kInlineDentry)) {
      return FindInInlineDir(name, res_page);
    }

    fbl::RefPtr<Page> dentry_page;
    if (FindDataPage(*cache_page_index, &dentry_page) != ZX_OK) {
      return nullptr;
    }

    uint64_t max_slots = 0;
    f2fs_hash_t name_hash = DentryHash(name);
    if (DirEntry *de = FindInBlock(dentry_page, name, &max_slots, name_hash, res_page);
        de != nullptr) {
      return de;
    }
    Page::PutPage(std::move(dentry_page), false);
  }
#endif  // __Fuchsia__

  return FindEntryOnDevice(name, res_page);
}

zx::status<DirEntry> Dir::FindEntry(std::string_view name) {
  DirEntry *de = nullptr;

#ifdef __Fuchsia__
  auto element = Vfs()->GetDirEntryCache().LookupDirEntry(Ino(), name);
  if (!element.is_error()) {
    return zx::ok(*element);
  }
#endif  // __Fuchsia__

  fbl::RefPtr<Page> page;

  de = FindEntryOnDevice(name, &page);

  if (de != nullptr) {
    DirEntry ret = *de;
    Page::PutPage(std::move(page), false);
    return zx::ok(ret);
  }

  return zx::error(ZX_ERR_NOT_FOUND);
}

DirEntry *Dir::ParentDir(fbl::RefPtr<Page> *out) {
  DirEntry *de = nullptr;
  DentryBlock *dentry_blk = nullptr;

  if (TestFlag(InodeInfoFlag::kInlineDentry))
    return ParentInlineDir(out);

  if (GetLockDataPage(0, out) != ZX_OK)
    return nullptr;

#if 0  // porting needed
  // dentry_blk = kmap(page);
#endif
  dentry_blk = static_cast<DentryBlock *>((*out)->GetAddress());
  de = &dentry_blk->dentry[1];
  (*out)->Unlock();
  return de;
}

ino_t Dir::InodeByName(std::string_view name) {
  if (auto dir_entry = FindEntry(name); !dir_entry.is_error()) {
    return LeToCpu((*dir_entry).ino);
  }

  return 0;
}

void Dir::SetLink(DirEntry *de, Page *page, VnodeF2fs *vnode) {
  page->Lock();
  page->WaitOnWriteback();
  de->ino = CpuToLe(vnode->Ino());
  SetDeType(de, vnode);
#if 0  // porting needed
  // if (!TestFlag(InodeInfoFlag::kInlineDentry))
  //   kunmap(page);
#else
  // If |de| is an inline dentry, the inode block should be flushed.
  // Otherwise, it writes out the data block.
  page->SetDirty();
#endif

#ifdef __Fuchsia__
  Vfs()->GetDirEntryCache().UpdateDirEntry(Ino(), vnode->GetName(), *de, page->GetIndex());
#endif  // __Fuchsia__

  timespec cur_time;
  clock_gettime(CLOCK_REALTIME, &cur_time);
  SetCTime(cur_time);
  SetMTime(cur_time);

  page->Unlock();
  MarkInodeDirty();
}

void Dir::InitDentInode(VnodeF2fs *vnode, Page *ipage) {
  if (!ipage)
    return;

  ipage->WaitOnWriteback();

  // copy dentry info. to this inode page
  Node *rn = static_cast<Node *>(ipage->GetAddress());
  rn->i.i_namelen = CpuToLe(vnode->GetNameLen());
  memcpy(rn->i.i_name, vnode->GetName(), vnode->GetNameLen());
  ipage->SetDirty();
}

zx_status_t Dir::InitInodeMetadata(VnodeF2fs *vnode) {
  if (vnode->TestFlag(InodeInfoFlag::kNewInode)) {
    if (zx_status_t err = Vfs()->GetNodeManager().NewInodePage(this, vnode); err != ZX_OK)
      return err;

    if (vnode->IsDir()) {
      if (zx_status_t err = MakeEmpty(vnode); err != ZX_OK) {
        Vfs()->GetNodeManager().RemoveInodePage(vnode);
        return err;
      }
      // TODO: need to check other points for nlink
      vnode->IncNlink();
    }

#if 0  // porting needed
    // err = f2fs_init_acl(inode, dir);
    // if (err) {
    //   remove_inode_page(inode);
    //   return err;
    // }
#endif
  } else {
    fbl::RefPtr<Page> ipage;

    if (zx_status_t err = Vfs()->GetNodeManager().GetNodePage(vnode->Ino(), &ipage); err != ZX_OK) {
      return err;
    }
    InitDentInode(vnode, ipage.get());
    Page::PutPage(std::move(ipage), true);
  }
  if (vnode->TestFlag(InodeInfoFlag::kIncLink)) {
    vnode->IncNlink();
    vnode->WriteInode(false);
  }
  return 0;
}

void Dir::UpdateParentMetadata(VnodeF2fs *vnode, unsigned int current_depth) {
  if (vnode->TestFlag(InodeInfoFlag::kNewInode)) {
    if (vnode->IsDir()) {
      IncNlink();
    }
    vnode->ClearFlag(InodeInfoFlag::kNewInode);
  }

  vnode->SetParentNid(Ino());
  timespec cur_time;
  clock_gettime(CLOCK_REALTIME, &cur_time);
  SetCTime(cur_time);
  SetMTime(cur_time);

  if (GetCurDirDepth() != current_depth) {
    SetCurDirDepth(current_depth);
  }

  MarkInodeDirty();

  if (vnode->TestFlag(InodeInfoFlag::kIncLink)) {
    vnode->ClearFlag(InodeInfoFlag::kIncLink);
  }
}

int Dir::RoomForFilename(DentryBlock *dentry_blk, int slots) {
  int bit_start = 0;
  int zero_start, zero_end;

  while (true) {
    zero_start = FindNextZeroBit(dentry_blk->dentry_bitmap, kNrDentryInBlock, bit_start);
    if (zero_start >= kNrDentryInBlock)
      return kNrDentryInBlock;

    zero_end = FindNextBit(dentry_blk->dentry_bitmap, kNrDentryInBlock, zero_start);
    if (zero_end - zero_start >= slots)
      return zero_start;

    bit_start = zero_end + 1;

    if (zero_end + 1 >= kNrDentryInBlock)
      return kNrDentryInBlock;
  }
}

zx_status_t Dir::AddLink(std::string_view name, VnodeF2fs *vnode) {
  unsigned int bit_pos;
  unsigned int level;
  unsigned int current_depth;
  uint64_t bidx;
  f2fs_hash_t dentry_hash;
  DirEntry *de;
  unsigned int nbucket, nblock;
  int namelen = static_cast<int>(name.length());
  fbl::RefPtr<Page> dentry_page;
  DentryBlock *dentry_blk = nullptr;
  int slots = (namelen + kNameLen - 1) / kNameLen;
  zx_status_t err = 0;

  if (TestFlag(InodeInfoFlag::kInlineDentry)) {
    bool is_converted = false;
    if (err = AddInlineEntry(name, vnode, &is_converted); err != ZX_OK)
      return err;

    if (!is_converted)
      return ZX_OK;
  }

  dentry_hash = DentryHash(name);
  level = 0;
  current_depth = static_cast<unsigned int>(GetCurDirDepth());
  if (IsSameDirHash(dentry_hash)) {
    level = static_cast<unsigned int>(GetDirHashLevel());
    ClearDirHash();
  }

  while (true) {
    if (current_depth == kMaxDirHashDepth)
      return ZX_ERR_OUT_OF_RANGE;

    /* Increase the depth, if required */
    if (level == current_depth)
      ++current_depth;

    nbucket = DirBuckets(level, GetDirLevel());
    nblock = BucketBlocks(level);

    bidx = DirBlockIndex(level, GetDirLevel(), (dentry_hash % nbucket));

    for (uint64_t block = bidx; block <= (bidx + nblock - 1); ++block) {
      if (err = GetNewDataPage(safemath::checked_cast<pgoff_t>(block), true, &dentry_page);
          err != ZX_OK) {
        return err;
      }

      dentry_blk = static_cast<DentryBlock *>(dentry_page->GetAddress());
      bit_pos = RoomForFilename(dentry_blk, slots);
      if (bit_pos < kNrDentryInBlock) {
        // porting needed
        // if (err = InitInodeMetadata(vnode, dentry); err == ZX_OK) {
        if (err = InitInodeMetadata(vnode); err == ZX_OK) {
          dentry_page->WaitOnWriteback();

          de = &dentry_blk->dentry[bit_pos];
          de->hash_code = CpuToLe(dentry_hash);
          de->name_len = CpuToLe(static_cast<uint16_t>(namelen));
          memcpy(dentry_blk->filename[bit_pos], name.data(), namelen);
          de->ino = CpuToLe(vnode->Ino());
          SetDeType(de, vnode);
          for (int i = 0; i < slots; ++i) {
            TestAndSetBit(bit_pos + i, dentry_blk->dentry_bitmap);
          }
          dentry_page->SetDirty();
#ifdef __Fuchsia__
          if (de != nullptr) {
            Vfs()->GetDirEntryCache().UpdateDirEntry(Ino(), name, *de, dentry_page->GetIndex());
          }
#endif  // __Fuchsia__
          UpdateParentMetadata(vnode, current_depth);
        }

        if (TestFlag(InodeInfoFlag::kUpdateDir)) {
          WriteInode(false);
          ClearFlag(InodeInfoFlag::kUpdateDir);
        }

        Page::PutPage(std::move(dentry_page), true);
        return err;
      }

      Page::PutPage(std::move(dentry_page), true);
    }

    /* Move to next level to find the empty slot for new dentry */
    ++level;
  }
}

/**
 * It only removes the dentry from the dentry page,corresponding name
 * entry in name page does not need to be touched during deletion.
 */
void Dir::DeleteEntry(DirEntry *dentry, Page *page, VnodeF2fs *vnode) {
  DentryBlock *dentry_blk;
  unsigned int bit_pos;
  int slots = (LeToCpu(dentry->name_len) + kNameLen - 1) / kNameLen;

  if (TestFlag(InodeInfoFlag::kInlineDentry)) {
    DeleteInlineEntry(dentry, page, vnode);
    return;
  }

  page->Lock();
  page->WaitOnWriteback();

  dentry_blk = static_cast<DentryBlock *>(page->GetAddress());
  bit_pos = static_cast<uint32_t>(dentry - dentry_blk->dentry);
  for (int i = 0; i < slots; ++i) {
    TestAndClearBit(bit_pos + i, dentry_blk->dentry_bitmap);
  }

#if 0  // porting needed
  // kunmap(page); /* kunmap - pair of f2fs_find_entry */
#endif
  page->SetDirty();

#ifdef __Fuchsia__
  std::string_view remove_name(reinterpret_cast<char *>(dentry_blk->filename[bit_pos]),
                               LeToCpu(dentry->name_len));

  Vfs()->GetDirEntryCache().RemoveDirEntry(Ino(), remove_name);
#endif  // __Fuchsia__

  timespec cur_time;
  clock_gettime(CLOCK_REALTIME, &cur_time);
  SetCTime(cur_time);
  SetMTime(cur_time);

  if (!vnode || !vnode->IsDir()) {
    MarkInodeDirty();
  }

  if (vnode) {
    if (vnode->IsDir()) {
      DropNlink();
      WriteInode(false);
    }

    vnode->SetCTime(cur_time);
    vnode->DropNlink();
    if (vnode->IsDir()) {
      vnode->DropNlink();
      vnode->InitSize();
    }
    vnode->WriteInode(false);
    if (vnode->GetNlink() == 0) {
      Vfs()->AddOrphanInode(vnode);
    }
  }

  // check and deallocate dentry page if all dentries of the page are freed
  bit_pos = FindNextBit(dentry_blk->dentry_bitmap, kNrDentryInBlock, 0);
  page->Unlock();

  if (bit_pos == kNrDentryInBlock) {
    TruncateHole(page->GetIndex(), page->GetIndex() + 1);
  }
}

zx_status_t Dir::MakeEmpty(VnodeF2fs *vnode) {
  if (vnode->TestFlag(InodeInfoFlag::kInlineDentry))
    return MakeEmptyInlineDir(vnode);

  fbl::RefPtr<Page> dentry_page;
  if (zx_status_t err = vnode->GetNewDataPage(0, true, &dentry_page); err != ZX_OK)
    return err;
#if 0  // porting needed
  // kaddr = kmap_atomic(dentry_page);
#endif
  DentryBlock *dentry_blk = static_cast<DentryBlock *>(dentry_page->GetAddress());

  DirEntry *de = &dentry_blk->dentry[0];
  de->name_len = CpuToLe(static_cast<uint16_t>(1));
  de->hash_code = 0;
  de->ino = CpuToLe(vnode->Ino());
  memcpy(dentry_blk->filename[0], ".", 1);
  SetDeType(de, vnode);

  de = &dentry_blk->dentry[1];
  de->hash_code = 0;
  de->name_len = CpuToLe(static_cast<uint16_t>(2));
  de->ino = CpuToLe(Ino());
  memcpy(dentry_blk->filename[1], "..", 2);
  SetDeType(de, vnode);

  TestAndSetBit(0, dentry_blk->dentry_bitmap);
  TestAndSetBit(1, dentry_blk->dentry_bitmap);
#if 0  // porting needed
  // kunmap_atomic(kaddr);
#endif
  dentry_page->SetDirty();
  Page::PutPage(std::move(dentry_page), true);
  return 0;
}

bool Dir::IsEmptyDir() {
  unsigned int bit_pos;
  DentryBlock *dentry_blk;
  uint64_t nblock = DirBlocks();

  if (TestFlag(InodeInfoFlag::kInlineDentry))
    return IsEmptyInlineDir();

  for (uint64_t bidx = 0; bidx < nblock; ++bidx) {
    fbl::RefPtr<Page> dentry_page;

    if (zx_status_t ret = GetLockDataPage(bidx, &dentry_page); ret != ZX_OK) {
      if (ret == ZX_ERR_NOT_FOUND)
        continue;
      else
        return false;
    }

#if 0  // porting needed
    // kaddr = kmap_atomic(dentry_page);
#endif
    dentry_blk = static_cast<DentryBlock *>(dentry_page->GetAddress());
    if (bidx == 0)
      bit_pos = 2;
    else
      bit_pos = 0;
    bit_pos = FindNextBit(dentry_blk->dentry_bitmap, kNrDentryInBlock, bit_pos);
#if 0  // porting needed
    // kunmap_atomic(kaddr);
#endif

    Page::PutPage(std::move(dentry_page), true);

    if (bit_pos < kNrDentryInBlock)
      return false;
  }
  return true;
}

zx_status_t Dir::Readdir(fs::VdirCookie *cookie, void *dirents, size_t len, size_t *out_actual) {
  fs::DirentFiller df(dirents, len);
  uint64_t *pos_cookie = reinterpret_cast<uint64_t *>(cookie);
  uint64_t pos = *pos_cookie;
  uint64_t npages = DirBlocks();
  unsigned int bit_pos = 0, start_bit_pos = 0;
  DentryBlock *dentry_blk = nullptr;
  DirEntry *de = nullptr;
  fbl::RefPtr<Page> dentry_page;
  unsigned char d_type = DT_UNKNOWN;
  int slots;
  zx_status_t ret = ZX_OK;
  bool done = false;

  if (GetSize() == 0) {
    *out_actual = 0;
    return ZX_OK;
  }

  if (TestFlag(InodeInfoFlag::kInlineDentry))
    return ReadInlineDir(cookie, dirents, len, out_actual);

  const unsigned char *types = kFiletypeTable;
  bit_pos = (pos % kNrDentryInBlock);

  for (uint64_t n = (pos / kNrDentryInBlock); n < npages; ++n) {
    if (ret = GetLockDataPage(n, &dentry_page); ret != ZX_OK)
      continue;

    start_bit_pos = bit_pos;
    dentry_blk = static_cast<DentryBlock *>(dentry_page->GetAddress());
    while (bit_pos < kNrDentryInBlock) {
      d_type = DT_UNKNOWN;
      bit_pos = FindNextBit(dentry_blk->dentry_bitmap, kNrDentryInBlock, bit_pos);
      if (bit_pos >= kNrDentryInBlock)
        break;

      de = &dentry_blk->dentry[bit_pos];
      if (types && de->file_type < static_cast<uint8_t>(FileType::kFtMax))
        d_type = types[de->file_type];

      std::string_view name(reinterpret_cast<char *>(dentry_blk->filename[bit_pos]),
                            LeToCpu(de->name_len));

      if (de->ino && name != "..") {
        if ((ret = df.Next(name, d_type, LeToCpu(de->ino))) != ZX_OK) {
          *pos_cookie += bit_pos - start_bit_pos;
          done = true;
          ret = ZX_OK;
          break;
        }
      }

      slots = (LeToCpu(de->name_len) + kNameLen - 1) / kNameLen;
      bit_pos += slots;
    }
    if (done)
      break;

    bit_pos = 0;
    *pos_cookie = (n + 1) * kNrDentryInBlock;

    Page::PutPage(std::move(dentry_page), true);
  }

  if (dentry_page && ret == ZX_OK) {
    Page::PutPage(std::move(dentry_page), true);
  }

  *out_actual = df.BytesFilled();

  return ret;
}

}  // namespace f2fs
