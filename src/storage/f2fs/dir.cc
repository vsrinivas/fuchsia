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
  DentryBlock *dentry_blk = dentry_page->GetAddress<DentryBlock>();
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
    fs()->GetDirEntryCache().UpdateDirEntry(Ino(), name, *de, (*res_page)->GetIndex());
  }
#endif  // __Fuchsia__

  return de;
}

DirEntry *Dir::FindEntry(std::string_view name, fbl::RefPtr<Page> *res_page) {
#ifdef __Fuchsia__
  if (auto cache_page_index = fs()->GetDirEntryCache().LookupDataPageIndex(Ino(), name);
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
  }
#endif  // __Fuchsia__

  return FindEntryOnDevice(name, res_page);
}

DirEntry *Dir::FindEntrySafe(std::string_view name, fbl::RefPtr<Page> *res_page) {
  fs::SharedLock dir_lock(dir_mutex_);
  return FindEntry(name, res_page);
}

zx::result<DirEntry> Dir::FindEntry(std::string_view name) {
  DirEntry *de = nullptr;

#ifdef __Fuchsia__
  auto element = fs()->GetDirEntryCache().LookupDirEntry(Ino(), name);
  if (!element.is_error()) {
    return zx::ok(*element);
  }
#endif  // __Fuchsia__

  fbl::RefPtr<Page> page;

  de = FindEntryOnDevice(name, &page);

  if (de != nullptr) {
    DirEntry ret = *de;
    return zx::ok(ret);
  }

  return zx::error(ZX_ERR_NOT_FOUND);
}

DirEntry *Dir::ParentDir(fbl::RefPtr<Page> *out) {
  DirEntry *de = nullptr;
  DentryBlock *dentry_blk = nullptr;

  fs::SharedLock dir_lock(dir_mutex_);
  if (TestFlag(InodeInfoFlag::kInlineDentry)) {
    return ParentInlineDir(out);
  }

  LockedPage page;
  if (GetLockedDataPage(0, &page) != ZX_OK) {
    return nullptr;
  }

  dentry_blk = page->GetAddress<DentryBlock>();
  de = &dentry_blk->dentry[1];
  *out = page.release();
  return de;
}

void Dir::SetLink(DirEntry *de, fbl::RefPtr<Page> &page, VnodeF2fs *vnode) {
  {
    LockedPage page_lock(page);
    page->WaitOnWriteback();
    de->ino = CpuToLe(vnode->Ino());
    SetDeType(de, vnode);
    // If |de| is an inline dentry, the inode block should be flushed.
    // Otherwise, it writes out the data block.
    page->SetDirty();

#ifdef __Fuchsia__
    fs()->GetDirEntryCache().UpdateDirEntry(Ino(), vnode->GetNameView(), *de, page->GetIndex());
#endif  // __Fuchsia__

    timespec cur_time;
    clock_gettime(CLOCK_REALTIME, &cur_time);
    SetCTime(cur_time);
    SetMTime(cur_time);
  }

  MarkInodeDirty();
}

void Dir::SetLinkSafe(DirEntry *de, fbl::RefPtr<Page> &page, VnodeF2fs *vnode) {
  std::lock_guard dir_lock(dir_mutex_);
  SetLink(de, page, vnode);
}

void Dir::InitDentInode(VnodeF2fs *vnode, NodePage &ipage) {
  ipage.WaitOnWriteback();

  // copy name info. to this inode page
  Node *rn = ipage.GetAddress<Node>();
  std::string_view name = vnode->GetNameView();
  // double check |name|
  ZX_DEBUG_ASSERT(IsValidNameLength(name));
  auto size = safemath::checked_cast<uint32_t>(name.size());
  rn->i.i_namelen = CpuToLe(size);
  name.copy(reinterpret_cast<char *>(&rn->i.i_name[0]), size);
  ipage.SetDirty();
}

zx_status_t Dir::InitInodeMetadata(VnodeF2fs *vnode) {
  if (vnode->TestFlag(InodeInfoFlag::kNewInode)) {
    if (auto page_or = fs()->GetNodeManager().NewInodePage(*vnode); page_or.is_error()) {
      return page_or.error_value();
    } else {
      InitDentInode(vnode, (*page_or).GetPage<NodePage>());
    }

    if (vnode->IsDir()) {
      if (zx_status_t err = MakeEmpty(vnode); err != ZX_OK) {
        fs()->GetNodeManager().RemoveInodePage(vnode);
        return err;
      }
    }

#if 0  // porting needed
    // err = f2fs_init_acl(inode, dir);
    // if (err) {
    //   remove_inode_page(inode);
    //   return err;
    // }
#endif
  } else {
    LockedPage ipage;
    if (zx_status_t err = fs()->GetNodeManager().GetNodePage(vnode->Ino(), &ipage); err != ZX_OK) {
      return err;
    }
    InitDentInode(vnode, ipage.GetPage<NodePage>());
  }

  if (vnode->TestFlag(InodeInfoFlag::kIncLink)) {
    vnode->IncNlink();
    vnode->WriteInode(false);
  }
  return ZX_OK;
}

void Dir::UpdateParentMetadata(VnodeF2fs *vnode, unsigned int current_depth) {
  if (vnode->ClearFlag(InodeInfoFlag::kNewInode)) {
    if (vnode->IsDir()) {
      IncNlink();
      SetFlag(InodeInfoFlag::kUpdateDir);
    }
  }

  vnode->SetParentNid(Ino());
  timespec cur_time;
  clock_gettime(CLOCK_REALTIME, &cur_time);
  SetCTime(cur_time);
  SetMTime(cur_time);

  if (GetCurDirDepth() != current_depth) {
    SetCurDirDepth(current_depth);
    SetFlag(InodeInfoFlag::kUpdateDir);
  }

  MarkInodeDirty();

  vnode->ClearFlag(InodeInfoFlag::kIncLink);
}

uint32_t Dir::RoomForFilename(DentryBlock *dentry_blk, uint32_t slots) {
  uint32_t bit_start = 0;

  while (true) {
    uint32_t zero_start, zero_end;
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
  int namelen = static_cast<int>(name.length());
  int slots = (namelen + kNameLen - 1) / kNameLen;
  auto umount = fit::defer([&] {
    if (ClearFlag(InodeInfoFlag::kUpdateDir)) {
      WriteInode(false);
    }
  });

  if (TestFlag(InodeInfoFlag::kInlineDentry)) {
    if (auto converted_or = AddInlineEntry(name, vnode); converted_or.is_error()) {
      return converted_or.error_value();
    } else if (!converted_or.value()) {
      return ZX_OK;
    }
  }

  uint32_t level = 0;
  f2fs_hash_t dentry_hash = DentryHash(name);
  if (IsSameDirHash(dentry_hash)) {
    level = static_cast<unsigned int>(GetDirHashLevel());
    ClearDirHash();
  }

  for (auto current_depth = GetCurDirDepth(); current_depth < kMaxDirHashDepth; ++level) {
    // Increase the depth, if required
    if (level == current_depth) {
      ++current_depth;
    }

    uint32_t nbucket = DirBuckets(level, GetDirLevel());
    uint32_t nblock = BucketBlocks(level);
    uint64_t bidx = DirBlockIndex(level, GetDirLevel(), (dentry_hash % nbucket));

    for (uint64_t block = bidx; block <= (bidx + nblock - 1); ++block) {
      zx_status_t err;
      LockedPage dentry_page;
      if (err = GetNewDataPage(safemath::checked_cast<pgoff_t>(block), true, &dentry_page);
          err != ZX_OK) {
        return err;
      }

      DentryBlock *dentry_blk = dentry_page->GetAddress<DentryBlock>();
      uint32_t bit_pos = RoomForFilename(dentry_blk, slots);
      if (bit_pos < kNrDentryInBlock) {
        if (err = InitInodeMetadata(vnode); err == ZX_OK) {
          dentry_page->WaitOnWriteback();

          DirEntry *de = &dentry_blk->dentry[bit_pos];
          de->hash_code = CpuToLe(dentry_hash);
          de->name_len = CpuToLe(safemath::checked_cast<uint16_t>(namelen));
          memcpy(dentry_blk->filename[bit_pos], name.data(), namelen);
          de->ino = CpuToLe(vnode->Ino());
          SetDeType(de, vnode);
          for (int i = 0; i < slots; ++i) {
            TestAndSetBit(bit_pos + i, dentry_blk->dentry_bitmap);
          }
          dentry_page->SetDirty();
#ifdef __Fuchsia__
          fs()->GetDirEntryCache().UpdateDirEntry(Ino(), name, *de, dentry_page->GetIndex());
#endif  // __Fuchsia__
          UpdateParentMetadata(vnode, safemath::checked_cast<uint32_t>(current_depth));
          return ZX_OK;
        }
        return err;
      }
    }
  }
  return ZX_ERR_OUT_OF_RANGE;
}

zx_status_t Dir::AddLinkSafe(std::string_view name, VnodeF2fs *vnode) {
  std::lock_guard dir_lock(dir_mutex_);
  return AddLink(name, vnode);
}

// It only removes the dentry from the dentry page, corresponding name
// entry in name page does not need to be touched during deletion.
void Dir::DeleteEntry(DirEntry *dentry, fbl::RefPtr<Page> &page, VnodeF2fs *vnode) {
  DentryBlock *dentry_blk;
  unsigned int bit_pos;
  int slots = (LeToCpu(dentry->name_len) + kNameLen - 1) / kNameLen;

  // Add to VnodeSet to ensure consistency of deleted entry.
  fs()->GetSuperblockInfo().AddVnodeToVnodeSet(InoType::kModifiedDirIno, Ino());

  if (TestFlag(InodeInfoFlag::kInlineDentry)) {
    DeleteInlineEntry(dentry, page, vnode);
    return;
  }

  {
    LockedPage page_lock(page);
    page->WaitOnWriteback();

    dentry_blk = page->GetAddress<DentryBlock>();
    bit_pos = static_cast<uint32_t>(dentry - dentry_blk->dentry);
    for (int i = 0; i < slots; ++i) {
      TestAndClearBit(bit_pos + i, dentry_blk->dentry_bitmap);
    }
    page->SetDirty();

#ifdef __Fuchsia__
    std::string_view remove_name(reinterpret_cast<char *>(dentry_blk->filename[bit_pos]),
                                 LeToCpu(dentry->name_len));

    fs()->GetDirEntryCache().RemoveDirEntry(Ino(), remove_name);
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
        fs()->AddOrphanInode(vnode);
      }
    }

    // check and deallocate dentry page if all dentries of the page are freed
    bit_pos = FindNextBit(dentry_blk->dentry_bitmap, kNrDentryInBlock, 0);
  }

  if (bit_pos == kNrDentryInBlock) {
    TruncateHole(page->GetIndex(), page->GetIndex() + 1);
  }
}

zx_status_t Dir::MakeEmpty(VnodeF2fs *vnode) {
  if (vnode->TestFlag(InodeInfoFlag::kInlineDentry))
    return MakeEmptyInlineDir(vnode);

  LockedPage dentry_page;
  if (zx_status_t err = vnode->GetNewDataPage(0, true, &dentry_page); err != ZX_OK)
    return err;

  DentryBlock *dentry_blk = dentry_page->GetAddress<DentryBlock>();

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

  dentry_page->SetDirty();
  return 0;
}

bool Dir::IsEmptyDir() {
  unsigned int bit_pos;
  DentryBlock *dentry_blk;

  uint64_t nblock = DirBlocks();
  if (TestFlag(InodeInfoFlag::kInlineDentry))
    return IsEmptyInlineDir();

  for (uint64_t bidx = 0; bidx < nblock; ++bidx) {
    LockedPage dentry_page;

    if (zx_status_t ret = GetLockedDataPage(bidx, &dentry_page); ret != ZX_OK) {
      if (ret == ZX_ERR_NOT_FOUND)
        continue;
      else
        return false;
    }

    dentry_blk = dentry_page->GetAddress<DentryBlock>();
    if (bidx == 0)
      bit_pos = 2;
    else
      bit_pos = 0;
    bit_pos = FindNextBit(dentry_blk->dentry_bitmap, kNrDentryInBlock, bit_pos);

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
  unsigned char d_type = DT_UNKNOWN;
  int slots;
  zx_status_t ret = ZX_OK;
  bool done = false;

  fs::SharedLock dir_lock(dir_mutex_);
  if (GetSize() == 0) {
    *out_actual = 0;
    return ZX_OK;
  }

  if (TestFlag(InodeInfoFlag::kInlineDentry))
    return ReadInlineDir(cookie, dirents, len, out_actual);

  const unsigned char *types = kFiletypeTable;
  bit_pos = (pos % kNrDentryInBlock);

  for (uint64_t n = (pos / kNrDentryInBlock); n < npages; ++n) {
    LockedPage dentry_page;
    if (ret = GetLockedDataPage(n, &dentry_page); ret != ZX_OK)
      continue;

    start_bit_pos = bit_pos;
    dentry_blk = dentry_page->GetAddress<DentryBlock>();
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
  }

  *out_actual = df.BytesFilled();

  return ret;
}

}  // namespace f2fs
