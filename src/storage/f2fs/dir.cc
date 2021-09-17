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

Dir::Dir(F2fs *fs) : VnodeF2fs(fs) {}

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
  uint32_t i;
  uint64_t bidx = 0;

  for (i = 0; i < level; i++) {
    bidx += DirBuckets(i, dir_level) * BucketBlocks(i);
  }
  bidx += idx * BucketBlocks(level);
  return bidx;
}

bool Dir::EarlyMatchName(const char *name, int namelen, f2fs_hash_t namehash, DirEntry *de) {
  if (LeToCpu(de->name_len) != namelen)
    return false;

  if (LeToCpu(de->hash_code) != namehash)
    return false;

  return true;
}

DirEntry *Dir::FindInBlock(Page *dentry_page, const char *name, int namelen, int *max_slots,
                           f2fs_hash_t namehash, Page **res_page) {
  DirEntry *de;
  uint32_t bit_pos, end_pos, next_pos;
#if 0  // porting needed
  // f2fs_dentry_block *dentry_blk = kmap(dentry_page);
#else
  DentryBlock *dentry_blk = reinterpret_cast<DentryBlock *>(dentry_page);
#endif
  int slots;

  bit_pos = FindNextBit(dentry_blk->dentry_bitmap, kNrDentryInBlock, 0);
  while (bit_pos < kNrDentryInBlock) {
    de = &dentry_blk->dentry[bit_pos];
    slots = (LeToCpu(de->name_len) + kNameLen - 1) / kNameLen;

    if (EarlyMatchName(name, namelen, namehash, de)) {
      if (!memcmp(dentry_blk->filename[bit_pos], name, namelen)) {
        *res_page = dentry_page;
        return de;
      }
    }
    next_pos = bit_pos + slots;
    bit_pos = FindNextBit(dentry_blk->dentry_bitmap, kNrDentryInBlock, next_pos);
    if (bit_pos >= kNrDentryInBlock)
      end_pos = kNrDentryInBlock;
    else
      end_pos = bit_pos;
    if (static_cast<uint64_t>(*max_slots) < end_pos - next_pos)
      *max_slots = end_pos - next_pos;
  }

  de = nullptr;
#if 0  // porting needed
//   kunmap(dentry_page);
#endif
  return de;
}

DirEntry *Dir::FindInLevel(unsigned int level, std::string_view name, int namelen,
                           f2fs_hash_t namehash, Page **res_page) {
  int s = (namelen + kNameLen - 1) / kNameLen;
  unsigned int nbucket, nblock;
  uint64_t bidx, end_block;
  Page *dentry_page = nullptr;
  DirEntry *de = nullptr;
  bool room = false;
  int max_slots = 0;

  ZX_ASSERT(level <= kMaxDirHashDepth);

  nbucket = DirBuckets(level, GetDirLevel());
  nblock = BucketBlocks(level);

  bidx = DirBlockIndex(level, GetDirLevel(), namehash % nbucket);
  end_block = bidx + nblock;

  for (; bidx < end_block; bidx++) {
    /* no need to allocate new dentry pages to all the indices */
    if (FindDataPage(bidx, &dentry_page) != ZX_OK) {
      room = true;
      continue;
    }

    if (de = FindInBlock(dentry_page, name.data(), namelen, &max_slots, namehash, res_page);
        de != nullptr)
      break;

    if (max_slots >= s)
      room = true;
    F2fsPutPage(dentry_page, 0);
  }

  if (!de && room && !IsSameDirHash(namehash)) {
    SetDirHash(namehash, level);
  }

  return de;
}

/*
 * Find an entry in the specified directory with the wanted name.
 * It returns the page where the entry was found (as a parameter - res_page),
 * and the entry itself. Page is returned mapped and unlocked.
 * Entry is guaranteed to be valid.
 */
DirEntry *Dir::FindEntry(std::string_view name, Page **res_page) {
  uint64_t npages = DirBlocks();
  DirEntry *de = nullptr;
  f2fs_hash_t name_hash;
  unsigned int max_depth;
  unsigned int level;

  fs::SharedLock read_lock(io_lock_);
  if (TestFlag(InodeInfoFlag::kInlineDentry)) {
    return FindInInlineDir(name, res_page);
  }

  if (npages == 0)
    return nullptr;

  *res_page = nullptr;

  name_hash = DentryHash(name.data(), static_cast<int>(name.length()));
  max_depth = static_cast<unsigned int>(GetCurDirDepth());

  for (level = 0; level < max_depth; level++) {
    if (de = FindInLevel(level, name, static_cast<int>(name.length()), name_hash, res_page);
        de != nullptr)
      break;
  }
  if (!de && !IsSameDirHash(name_hash)) {
    SetDirHash(name_hash, level - 1);
  }
  return de;
}

DirEntry *Dir::ParentDir(Page **p) {
  Page *page = nullptr;
  DirEntry *de = nullptr;
  DentryBlock *dentry_blk = nullptr;

  if (TestFlag(InodeInfoFlag::kInlineDentry))
    return ParentInlineDir(p);

  if (GetLockDataPage(0, &page) != ZX_OK)
    return nullptr;

#if 0  // porting needed
  // dentry_blk = kmap(page);
#endif
  dentry_blk = static_cast<DentryBlock *>(PageAddress(page));
  de = &dentry_blk->dentry[1];
  *p = page;
#if 0  // porting needed
  // unlock_page(page);
#endif
  return de;
}

ino_t Dir::InodeByName(std::string_view name) {
  ino_t res = 0;
  DirEntry *de;
  Page *page;

  if (de = FindEntry(name, &page); de != nullptr) {
    res = LeToCpu(de->ino);
#if 0  // porting needed
    // if (!TestFlag(InodeInfoFlag::kInlineDentry))
    //   kunmap(page);
#endif
    F2fsPutPage(page, 0);
  }

  return res;
}

void Dir::SetLink(DirEntry *de, Page *page, VnodeF2fs *vnode) {
  std::lock_guard write_lock(io_lock_);
#if 0  // porting needed
  // lock_page(page);
#endif
  WaitOnPageWriteback(page);
  de->ino = CpuToLe(vnode->Ino());
  SetDeType(de, vnode);
#if 0  // porting needed
  // if (!TestFlag(InodeInfoFlag::kInlineDentry))
  //   kunmap(page);
  // set_page_dirty(page);
#else
  // If |de| is an inline dentry, the inode block should be flushed.
  // Otherwise, it writes out the data block.
  if (page->host == this) {
    FlushDirtyDataPage(Vfs(), page);
  } else {
    ZX_ASSERT(page->host == nullptr);
    FlushDirtyNodePage(Vfs(), page);
  }
#endif

  timespec cur_time;
  clock_gettime(CLOCK_REALTIME, &cur_time);
  SetCTime(cur_time);
  SetMTime(cur_time);

  MarkInodeDirty();
  F2fsPutPage(page, 1);
}

void Dir::InitDentInode(VnodeF2fs *vnode, Page *ipage) {
#if 0  // porting needed
  // inode *dir = dentry->d_parent->d_inode;
#endif
  Node *rn;

  if (!ipage)
    return;

  WaitOnPageWriteback(ipage);

  /* copy dentry info. to this inode page */
  rn = static_cast<Node *>(PageAddress(ipage));
  rn->i.i_pino = CpuToLe(Ino());
  rn->i.i_namelen = CpuToLe(vnode->GetNameLen());
  memcpy(rn->i.i_name, vnode->GetName(), vnode->GetNameLen());
#if 0  // porting needed
  // set_page_dirty(ipage);
#else
  FlushDirtyNodePage(Vfs(), ipage);
#endif
}

zx_status_t Dir::InitInodeMetadata(VnodeF2fs *vnode) {
#if 0  // porting needed
  // inode *dir = dentry->d_parent->d_inode;
#endif

  if (vnode->TestFlag(InodeInfoFlag::kNewInode)) {
    if (zx_status_t err = Vfs()->GetNodeManager().NewInodePage(this, vnode); err != ZX_OK)
      return err;

    if (vnode->IsDir()) {
      if (zx_status_t err = MakeEmpty(vnode, this); err != ZX_OK) {
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
    Page *ipage = nullptr;

    if (zx_status_t err = Vfs()->GetNodeManager().GetNodePage(vnode->Ino(), &ipage); err != ZX_OK)
      return err;
    InitDentInode(vnode, ipage);
    F2fsPutPage(ipage, 1);
  }
  if (vnode->TestFlag(InodeInfoFlag::kIncLink)) {
    vnode->IncNlink();
    vnode->WriteInode(nullptr);
  }
  return 0;
}

void Dir::UpdateParentMetadata(VnodeF2fs *vnode, unsigned int current_depth) {
  bool need_dir_update = false;

  if (vnode->TestFlag(InodeInfoFlag::kNewInode)) {
    if (vnode->IsDir()) {
      IncNlink();
      need_dir_update = true;
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
    need_dir_update = true;
  }

  if (need_dir_update) {
    WriteInode(nullptr);
  } else {
    MarkInodeDirty();
  }

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
  uint64_t bidx, block;
  f2fs_hash_t dentry_hash;
  DirEntry *de;
  unsigned int nbucket, nblock;
  int namelen = static_cast<int>(name.length());
  Page *dentry_page = nullptr;
  DentryBlock *dentry_blk = nullptr;
  int slots = (namelen + kNameLen - 1) / kNameLen;
  zx_status_t err = 0;
  int i;

  if (TestFlag(InodeInfoFlag::kInlineDentry)) {
    bool is_converted = false;
    std::lock_guard write_lock(io_lock_);
    if (err = AddInlineEntry(name, vnode, &is_converted); err != ZX_OK)
      return err;

    if (!is_converted)
      return ZX_OK;
  }

  dentry_hash = DentryHash(name.data(), namelen);
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

    for (block = bidx; block <= (bidx + nblock - 1); block++) {
      std::lock_guard write_lock(io_lock_);
      if (err = GetNewDataPage(block, true, &dentry_page); err != ZX_OK) {
        return err;
      }

      // porting needed
      // dentry_blk = kmap(dentry_page);
      dentry_blk = reinterpret_cast<DentryBlock *>(dentry_page->data);
      bit_pos = RoomForFilename(dentry_blk, slots);
      if (bit_pos < kNrDentryInBlock) {
        // porting needed
        // if (err = InitInodeMetadata(vnode, dentry); err == ZX_OK) {
        if (err = InitInodeMetadata(vnode); err == ZX_OK) {
          WaitOnPageWriteback(dentry_page);

          de = &dentry_blk->dentry[bit_pos];
          de->hash_code = CpuToLe(dentry_hash);
          de->name_len = CpuToLe(static_cast<uint16_t>(namelen));
          memcpy(dentry_blk->filename[bit_pos], name.data(), namelen);
          de->ino = CpuToLe(vnode->Ino());
          SetDeType(de, vnode);
          for (i = 0; i < slots; i++)
            TestAndSetBit(bit_pos + i, dentry_blk->dentry_bitmap);
#if 0  // porting needed
       // set_page_dirty(dentry_page);
#else
          FlushDirtyDataPage(Vfs(), dentry_page);
#endif
          UpdateParentMetadata(vnode, current_depth);
        }

        if (TestFlag(InodeInfoFlag::kUpdateDir)) {
          WriteInode(nullptr);
          ClearFlag(InodeInfoFlag::kUpdateDir);
        }

        // porting needed
        // kunmap(dentry_page);
        F2fsPutPage(dentry_page, 1);
        return err;
      }

      // porting needed
      // kunmap(dentry_page);
      F2fsPutPage(dentry_page, 1);
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
#if 0  // porting needed
  // address_space *mapping = page->mapping;
#endif
  SbInfo &sbi = Vfs()->GetSbInfo();
  int slots = (LeToCpu(dentry->name_len) + kNameLen - 1) / kNameLen;
  void *kaddr = PageAddress(page);
  int i;

  std::lock_guard write_lock(io_lock_);

  if (TestFlag(InodeInfoFlag::kInlineDentry)) {
    DeleteInlineEntry(dentry, page, vnode);
    return;
  }

#if 0  // porting needed
  // lock_page(page);
#endif
  WaitOnPageWriteback(page);

  dentry_blk = static_cast<DentryBlock *>(kaddr);
  bit_pos = static_cast<uint32_t>(dentry - dentry_blk->dentry);
  for (i = 0; i < slots; i++)
    TestAndClearBit(bit_pos + i, dentry_blk->dentry_bitmap);

  /* Let's check and deallocate this dentry page */
  bit_pos = FindNextBit(dentry_blk->dentry_bitmap, kNrDentryInBlock, 0);
#if 0  // porting needed
  // kunmap(page); /* kunmap - pair of f2fs_find_entry */
  // set_page_dirty(page);
#else
  FlushDirtyDataPage(Vfs(), page);
#endif

  timespec cur_time;
  clock_gettime(CLOCK_REALTIME, &cur_time);
  SetCTime(cur_time);
  SetMTime(cur_time);

  if (vnode && vnode->IsDir()) {
    DropNlink();
    WriteInode(nullptr);
  } else {
    MarkInodeDirty();
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
    vnode->WriteInode(nullptr);
    if (vnode->GetNlink() == 0) {
      Vfs()->AddOrphanInode(vnode);
    }
  }

  if (bit_pos == kNrDentryInBlock) {
    __UNUSED loff_t page_offset;
    TruncateHole(page->index, page->index + 1);
    ClearPageDirtyForIo(page);
#if 0  // porting needed
    // ClearPageUptodate(page);
#endif
    DecPageCount(&sbi, CountType::kDirtyDents);
    InodeDecDirtyDents(this);
    page_offset = page->index << kPageCacheShift;
    F2fsPutPage(page, 1);
  } else {
    F2fsPutPage(page, 1);
  }
}

zx_status_t Dir::MakeEmpty(VnodeF2fs *vnode, VnodeF2fs *parent) {
  Page *dentry_page = nullptr;
  DentryBlock *dentry_blk;
  DirEntry *de;
  void *kaddr;

  if (vnode->TestFlag(InodeInfoFlag::kInlineDentry))
    return MakeEmptyInlineDir(vnode, parent);

  if (zx_status_t err = vnode->GetNewDataPage(0, true, &dentry_page); err != ZX_OK)
    return err;

#if 0  // porting needed
  // kaddr = kmap_atomic(dentry_page);
#else
  kaddr = dentry_page->data;
#endif
  dentry_blk = static_cast<DentryBlock *>(kaddr);

  de = &dentry_blk->dentry[0];
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

  TestAndSetBit(0, dentry_blk->dentry_bitmap);
  TestAndSetBit(1, dentry_blk->dentry_bitmap);
#if 0  // porting needed
  // kunmap_atomic(kaddr);
  // set_page_dirty(dentry_page);
#else
  FlushDirtyDataPage(Vfs(), dentry_page);
#endif
  F2fsPutPage(dentry_page, 1);
  return 0;
}

bool Dir::IsEmptyDir() {
  uint64_t bidx;
  Page *dentry_page = nullptr;
  unsigned int bit_pos;
  DentryBlock *dentry_blk;
  uint64_t nblock = DirBlocks();

  if (TestFlag(InodeInfoFlag::kInlineDentry))
    return IsEmptyInlineDir();

  for (bidx = 0; bidx < nblock; bidx++) {
    void *kaddr;

    if (zx_status_t ret = GetLockDataPage(bidx, &dentry_page); ret != ZX_OK) {
      if (ret == ZX_ERR_NOT_FOUND)
        continue;
      else
        return false;
    }

#if 0  // porting needed
    // kaddr = kmap_atomic(dentry_page);
#else
    kaddr = dentry_page->data;
#endif
    dentry_blk = static_cast<DentryBlock *>(kaddr);
    if (bidx == 0)
      bit_pos = 2;
    else
      bit_pos = 0;
    bit_pos = FindNextBit(dentry_blk->dentry_bitmap, kNrDentryInBlock, bit_pos);
#if 0  // porting needed
    // kunmap_atomic(kaddr);
#endif

    F2fsPutPage(dentry_page, 1);

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
  Page *dentry_page = nullptr;
  uint64_t n = 0;
  unsigned char d_type = DT_UNKNOWN;
  int slots;
  zx_status_t ret = ZX_OK;
  bool done = false;

  fs::SharedLock read_lock(io_lock_);

  if (GetSize() == 0) {
    *out_actual = 0;
    return ZX_OK;
  }

  if (TestFlag(InodeInfoFlag::kInlineDentry))
    return ReadInlineDir(cookie, dirents, len, out_actual);

  const unsigned char *types = kFiletypeTable;
  bit_pos = (pos % kNrDentryInBlock);
  n = (pos / kNrDentryInBlock);

  for (; n < npages; n++) {
    if (ret = GetLockDataPage(n, &dentry_page); ret != ZX_OK)
      continue;

    start_bit_pos = bit_pos;
#if 0  // porting needed
    //     dentry_blk = kmap(dentry_page);
#else
    dentry_blk = reinterpret_cast<DentryBlock *>(dentry_page);
#endif
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

#if 0  // porting needed
    //     kunmap(dentry_page);
#endif
    F2fsPutPage(dentry_page, 1);
    dentry_page = nullptr;
  }

  if (dentry_page && ret == ZX_OK) {
#if 0  // porting needed
    //     kunmap(dentry_page);
#endif
    F2fsPutPage(dentry_page, 1);
  }

  *out_actual = df.BytesFilled();

  return ret;
}

#if 0  // porting needed
// const file_operations f2fs_dir_operations = {
//   .llseek    = generic_file_llseek,
//   .read    = generic_read_dir,
//   .readdir  = f2fs_readdir,
// };
#endif

}  // namespace f2fs
