// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dirent.h>
#include <stdint.h>
#include <string.h>
#include <sys/stat.h>

#include "src/storage/f2fs/f2fs.h"

namespace f2fs {

VnodeF2fs::VnodeF2fs(F2fs *fs) : Vnode(fs) {}

VnodeF2fs::VnodeF2fs(F2fs *fs, ino_t ino) : Vnode(fs), ino_(ino) {}

fs::VnodeProtocolSet VnodeF2fs::GetProtocols() const {
  if (IsDir()) {
    return fs::VnodeProtocol::kDirectory;
  } else {
    return fs::VnodeProtocol::kFile;
  }
}

void VnodeF2fs::SetMode(const umode_t &mode) { mode_ = mode; }

umode_t VnodeF2fs::GetMode() const { return mode_; }

bool VnodeF2fs::IsDir() const { return S_ISDIR(mode_); }

bool VnodeF2fs::IsReg() const { return S_ISREG(mode_); }

bool VnodeF2fs::IsLink() const { return S_ISLNK(mode_); }

bool VnodeF2fs::IsChr() const { return S_ISCHR(mode_); }

bool VnodeF2fs::IsBlk() const { return S_ISBLK(mode_); }

bool VnodeF2fs::IsSock() const { return S_ISSOCK(mode_); }

bool VnodeF2fs::IsFifo() const { return S_ISFIFO(mode_); }

bool VnodeF2fs::HasGid() const { return mode_ & S_ISGID; }

zx_status_t VnodeF2fs::GetNodeInfoForProtocol([[maybe_unused]] fs::VnodeProtocol protocol,
                                              [[maybe_unused]] fs::Rights rights,
                                              fs::VnodeRepresentation *info) {
  if (IsDir()) {
    *info = fs::VnodeRepresentation::Directory();
  } else {
    *info = fs::VnodeRepresentation::File();
  }
  return ZX_OK;
}

void VnodeF2fs::Allocate(F2fs *fs, ino_t ino, uint32_t mode, fbl::RefPtr<VnodeF2fs> *out) {
  // Check if ino is within scope
  fs->GetNodeManager().CheckNidRange(ino);
  if (S_ISDIR(mode)) {
    *out = fbl::MakeRefCounted<Dir>(fs, ino);
  } else {
    *out = fbl::MakeRefCounted<File>(fs, ino);
  }
  (*out)->Init();
}

zx_status_t VnodeF2fs::Create(F2fs *fs, ino_t ino, fbl::RefPtr<VnodeF2fs> *out) {
  Page *node_page = nullptr;

  if (ino == fs->GetSuperblockInfo().GetNodeIno() || ino == fs->GetSuperblockInfo().GetMetaIno()) {
    *out = fbl::MakeRefCounted<VnodeF2fs>(fs, ino);
    return ZX_OK;
  }

  /* Check if ino is within scope */
  fs->GetNodeManager().CheckNidRange(ino);

  if (fs->GetNodeManager().GetNodePage(ino, &node_page) != ZX_OK) {
    return ZX_ERR_NOT_FOUND;
  }

  Node *rn = static_cast<Node *>(PageAddress(node_page));
  Inode &ri = rn->i;

  if (S_ISDIR(ri.i_mode)) {
    *out = fbl::MakeRefCounted<Dir>(fs, ino);
  } else {
    *out = fbl::MakeRefCounted<File>(fs, ino);
  }

  VnodeF2fs *vnode = out->get();

  vnode->Init();
  vnode->SetMode(LeToCpu(ri.i_mode));
  vnode->SetUid(LeToCpu(ri.i_uid));
  vnode->SetGid(LeToCpu(ri.i_gid));
  vnode->SetNlink(ri.i_links);
  vnode->SetSize(LeToCpu(ri.i_size));
  vnode->SetBlocks(LeToCpu(ri.i_blocks));
  vnode->SetATime(LeToCpu(ri.i_atime), LeToCpu(ri.i_atime_nsec));
  vnode->SetCTime(LeToCpu(ri.i_ctime), LeToCpu(ri.i_ctime_nsec));
  vnode->SetMTime(LeToCpu(ri.i_mtime), LeToCpu(ri.i_mtime_nsec));
  vnode->SetGeneration(LeToCpu(ri.i_generation));
  vnode->SetParentNid(LeToCpu(ri.i_pino));
  vnode->SetCurDirDepth(LeToCpu(ri.i_current_depth));
  vnode->SetXattrNid(LeToCpu(ri.i_xattr_nid));
  vnode->SetInodeFlags(LeToCpu(ri.i_flags));
  vnode->SetDirLevel(ri.i_dir_level);
  vnode->fi_.data_version = LeToCpu(fs->GetSuperblockInfo().GetCheckpoint().checkpoint_ver) - 1;
  vnode->SetAdvise(ri.i_advise);
  vnode->GetExtentInfo(ri.i_ext);
  std::string_view name(reinterpret_cast<char *>(ri.i_name), std::min(kMaxNameLen, ri.i_namelen));
  if (ri.i_namelen != name.length() ||
      (ino != fs->GetSuperblockInfo().GetRootIno() && !fs::IsValidName(name))) {
    // TODO: Need to repair the file or set NeedFsck flag when fsck supports repair feature.
    // For now, we set kBad and clear link, so that it can be deleted without purging.
    fbl::RefPtr<VnodeF2fs> failed = std::move(*out);
    failed->ClearNlink();
    failed->SetFlag(InodeInfoFlag::kBad);
    out = nullptr;
    return ZX_ERR_NOT_FOUND;
  }

  vnode->SetName(name);

  if (ri.i_inline & kInlineDentry) {
    vnode->SetFlag(InodeInfoFlag::kInlineDentry);
  }
  if (ri.i_inline & kExtraAttr) {
    vnode->SetExtraISize(ri.i_extra_isize / sizeof(uint32_t));
  }

  F2fsPutPage(node_page, 1);
  return ZX_OK;
}

zx_status_t VnodeF2fs::OpenNode([[maybe_unused]] ValidatedOptions options,
                                fbl::RefPtr<Vnode> *out_redirect) {
  return ZX_OK;
}

zx_status_t VnodeF2fs::CloseNode() { return ZX_OK; }

void VnodeF2fs::RecycleNode() {
  {
    std::lock_guard lock(mutex_);
    ZX_ASSERT_MSG(open_count() == 0, "RecycleNode[%s:%u]: open_count must be zero (%lu)", GetName(),
                  GetKey(), open_count());
  }
  if (GetNlink()) {
    Vfs()->GetVCache().Downgrade(this);
  } else {
    EvictVnode();
    Deactivate();
    delete this;
  }
}

zx_status_t VnodeF2fs::GetAttributes(fs::VnodeAttributes *a) {
#ifdef F2FS_BU_DEBUG
  FX_LOGS(DEBUG) << "f2fs_getattr() vn=" << this << "(#" << ino_ << ")";
#endif
  *a = fs::VnodeAttributes();

  fs::SharedLock rlock(mutex_);
  a->mode = mode_;
  a->inode = ino_;
  a->content_size = size_;
  a->storage_size = GetBlockCount() * kBlockSize;
  a->link_count = nlink_;
  a->creation_time = zx_time_add_duration(ZX_SEC(ctime_.tv_sec), ctime_.tv_nsec);
  a->modification_time = zx_time_add_duration(ZX_SEC(mtime_.tv_sec), mtime_.tv_nsec);

  return ZX_OK;
}

zx_status_t VnodeF2fs::SetAttributes(fs::VnodeAttributesUpdate attr) {
  bool need_inode_sync = false;

  {
    std::lock_guard wlock(mutex_);
    if (attr.has_creation_time()) {
      SetCTime(zx_timespec_from_duration(attr.take_creation_time()));
      need_inode_sync = true;
    }
    if (attr.has_modification_time()) {
      SetMTime(zx_timespec_from_duration(attr.take_modification_time()));
      need_inode_sync = true;
    }
  }

  if (attr.any()) {
    return ZX_ERR_INVALID_ARGS;
  }

  if (need_inode_sync) {
    MarkInodeDirty();
  }

  return ZX_OK;
}

struct f2fs_iget_args {
  uint64_t ino;
  int on_free;
};

#if 0  // porting needed
// void VnodeF2fs::F2fsSetInodeFlags() {
  // uint64_t &flags = fi.i_flags;

  // inode_.i_flags &= ~(S_SYNC | S_APPEND | S_IMMUTABLE |
  //     S_NOATIME | S_DIRSYNC);

  // if (flags & FS_SYNC_FL)
  //   inode_.i_flags |= S_SYNC;
  // if (flags & FS_APPEND_FL)
  //   inode_.i_flags |= S_APPEND;
  // if (flags & FS_IMMUTABLE_FL)
  //   inode_.i_flags |= S_IMMUTABLE;
  // if (flags & FS_NOATIME_FL)
  //   inode_.i_flags |= S_NOATIME;
  // if (flags & FS_DIRSYNC_FL)
  //   inode_.i_flags |= S_DIRSYNC;
// }

// int VnodeF2fs::F2fsIgetTest(void *data) {
  // f2fs_iget_args *args = (f2fs_iget_args *)data;

  // if (ino_ != args->ino)
  //   return 0;
  // if (i_state & (I_FREEING | I_WILL_FREE)) {
  //   args->on_free = 1;
  //   return 0;
  // }
  // return 1;
// }

// VnodeF2fs *VnodeF2fs::F2fsIgetNowait(uint64_t ino) {
//   fbl::RefPtr<VnodeF2fs> vnode_refptr;
//   VnodeF2fs *vnode = nullptr;
//   f2fs_iget_args args = {.ino = ino, .on_free = 0};
//   vnode = ilookup5(sb, ino, F2fsIgetTest, &args);

//   if (vnode)
//     return vnode;
//   if (!args.on_free) {
//     Vget(Vfs(), ino, &vnode_refptr);
//     vnode = vnode_refptr.get();
//     return vnode;
//   }
//   return static_cast<VnodeF2fs *>(ErrPtr(ZX_ERR_NOT_FOUND));
// }
#endif

zx_status_t VnodeF2fs::Vget(F2fs *fs, ino_t ino, fbl::RefPtr<VnodeF2fs> *out) {
  fbl::RefPtr<VnodeF2fs> vnode_refptr;

  if (fs->LookupVnode(ino, &vnode_refptr) == ZX_OK) {
    vnode_refptr->WaitForInit();
    *out = std::move(vnode_refptr);
    return ZX_OK;
  }

  if (zx_status_t status = Create(fs, ino, &vnode_refptr); status != ZX_OK) {
    return status;
  }

  if (!(ino == fs->GetSuperblockInfo().GetNodeIno() ||
        ino == fs->GetSuperblockInfo().GetMetaIno())) {
    if (!fs->GetSuperblockInfo().IsOnRecovery() && vnode_refptr->GetNlink() == 0) {
      return ZX_ERR_NOT_FOUND;
    }
  }

  if (zx_status_t status = fs->InsertVnode(vnode_refptr.get()); status != ZX_OK) {
    vnode_refptr->SetFlag(InodeInfoFlag::kBad);
    vnode_refptr.reset();
    if (fs->LookupVnode(ino, &vnode_refptr) == ZX_OK) {
      vnode_refptr->WaitForInit();
      *out = std::move(vnode_refptr);
      return ZX_OK;
    }
  }

  vnode_refptr->UnlockNewInode();
  *out = std::move(vnode_refptr);

  return ZX_OK;
}

void VnodeF2fs::UpdateInode(Page *node_page) {
  Node *rn;
  Inode *ri;

  WaitOnPageWriteback(node_page);

  rn = static_cast<Node *>(PageAddress(node_page));
  ri = &(rn->i);

  ri->i_mode = CpuToLe(GetMode());
  ri->i_advise = GetAdvise();
  ri->i_uid = CpuToLe(GetUid());
  ri->i_gid = CpuToLe(GetGid());
  ri->i_links = CpuToLe(GetNlink());
  ri->i_size = CpuToLe(GetSize());
  ri->i_blocks = CpuToLe(GetBlocks());
  SetRawExtent(ri->i_ext);

  ri->i_atime = CpuToLe(static_cast<uint64_t>(atime_.tv_sec));
  ri->i_ctime = CpuToLe(static_cast<uint64_t>(ctime_.tv_sec));
  ri->i_mtime = CpuToLe(static_cast<uint64_t>(mtime_.tv_sec));
  ri->i_atime_nsec = CpuToLe(static_cast<uint32_t>(atime_.tv_nsec));
  ri->i_ctime_nsec = CpuToLe(static_cast<uint32_t>(ctime_.tv_nsec));
  ri->i_mtime_nsec = CpuToLe(static_cast<uint32_t>(mtime_.tv_nsec));
  ri->i_current_depth = CpuToLe(static_cast<uint32_t>(GetCurDirDepth()));
  ri->i_xattr_nid = CpuToLe(GetXattrNid());
  ri->i_flags = CpuToLe(GetInodeFlags());
  ri->i_generation = CpuToLe(GetGeneration());
  ri->i_dir_level = GetDirLevel();

  ri->i_namelen = CpuToLe(GetNameLen());
  memcpy(ri->i_name, GetName(), GetNameLen());

  if (TestFlag(InodeInfoFlag::kInlineDentry)) {
    ri->i_inline |= kInlineDentry;
  } else {
    ri->i_inline &= ~kInlineDentry;
  }
  if (GetExtraISize()) {
    ri->i_inline |= kExtraAttr;
    ri->i_extra_isize = GetExtraISize();
  }

#if 0  // porting needed
  // set_page_dirty(node_page);
#else
  FlushDirtyNodePage(Vfs(), node_page);
#endif
}

int VnodeF2fs::WriteInode(WritebackControl *wbc) {
  SuperblockInfo &superblock_info = Vfs()->GetSuperblockInfo();
  Page *node_page = nullptr;
  zx_status_t ret = ZX_OK;

  if (ino_ == superblock_info.GetNodeIno() || ino_ == superblock_info.GetMetaIno()) {
    return ret;
  }

  if (!IsDirty()) {
    return ZX_OK;
  }

  {
    fs::SharedLock rlock(superblock_info.GetFsLock(LockType::kNodeOp));
    if (ret = Vfs()->GetNodeManager().GetNodePage(ino_, &node_page); ret != ZX_OK)
      return ret;
    UpdateInode(node_page);
  }

  F2fsPutPage(node_page, 1);

  return ZX_OK;
}

zx_status_t VnodeF2fs::DoTruncate(size_t len) {
  zx_status_t ret;

  if (ret = TruncateBlocks(len); ret == ZX_OK) {
    SetSize(len);
    timespec cur_time;
    clock_gettime(CLOCK_REALTIME, &cur_time);
    SetCTime(cur_time);
    SetMTime(cur_time);
    MarkInodeDirty();
  }

  Vfs()->GetSegmentManager().BalanceFs();
  return ret;
}

int VnodeF2fs::TruncateDataBlocksRange(DnodeOfData *dn, int count) {
  int nr_free = 0, ofs = dn->ofs_in_node;
  Node *raw_node;
  uint32_t *addr;

  raw_node = static_cast<Node *>(PageAddress(dn->node_page));
  addr = BlkaddrInNode(raw_node) + ofs;

  for (; count > 0; count--, addr++, dn->ofs_in_node++) {
    block_t blkaddr = LeToCpu(*addr);
    if (blkaddr == kNullAddr)
      continue;

    UpdateExtentCache(kNullAddr, dn);
    Vfs()->GetSegmentManager().InvalidateBlocks(blkaddr);
    Vfs()->DecValidBlockCount(dn->vnode, 1);
    nr_free++;
  }
  if (nr_free) {
#if 0  // porting needed
    // set_page_dirty(dn->node_page);
#else
    FlushDirtyNodePage(Vfs(), dn->node_page);
#endif
    Vfs()->GetNodeManager().SyncInodePage(*dn);
  }
  dn->ofs_in_node = ofs;
  return nr_free;
}

void VnodeF2fs::TruncateDataBlocks(DnodeOfData *dn) { TruncateDataBlocksRange(dn, kAddrsPerBlock); }

void VnodeF2fs::TruncatePartialDataPage(uint64_t from) {
  size_t offset = from & (kPageCacheSize - 1);
  Page *page = nullptr;

  if (!offset)
    return;

  if (FindDataPage(from >> kPageCacheShift, &page) != ZX_OK)
    return;

#if 0  // porting needed
  // lock_page(page);
#endif
  WaitOnPageWriteback(page);
  zero_user(page, static_cast<uint32_t>(offset), static_cast<uint32_t>(kPageCacheSize - offset));
#if 0  // porting needed
  // set_page_dirty(page);
#else
  FlushDirtyDataPage(Vfs(), page);
#endif
  F2fsPutPage(page, 1);
}

zx_status_t VnodeF2fs::TruncateBlocks(uint64_t from) {
  SuperblockInfo &superblock_info = Vfs()->GetSuperblockInfo();
  unsigned int blocksize = superblock_info.GetBlocksize();
  DnodeOfData dn;
  int count = 0;
  zx_status_t err;

  if (from > GetSize())
    return ZX_OK;

  pgoff_t free_from =
      static_cast<pgoff_t>((from + blocksize - 1) >> (superblock_info.GetLogBlocksize()));

  {
    fs::SharedLock rlock(superblock_info.GetFsLock(LockType::kFileOp));
    std::lock_guard write_lock(io_lock_);

    do {
      NodeManager::SetNewDnode(dn, this, nullptr, nullptr, 0);
      err = Vfs()->GetNodeManager().GetDnodeOfData(dn, free_from, kRdOnlyNode);
      if (err) {
        if (err == ZX_ERR_NOT_FOUND)
          break;
        return err;
      }

      if (IsInode(dn.node_page))
        count = kAddrsPerInode;
      else
        count = kAddrsPerBlock;

      count -= dn.ofs_in_node;
      ZX_ASSERT(count >= 0);
      if (dn.ofs_in_node || IsInode(dn.node_page)) {
        TruncateDataBlocksRange(&dn, count);
        free_from += count;
      }

      F2fsPutDnode(&dn);
    } while (false);

    err = Vfs()->GetNodeManager().TruncateInodeBlocks(*this, free_from);
  }
  // lastly zero out the first data page
  TruncatePartialDataPage(from);

  return err;
}

zx_status_t VnodeF2fs::TruncateHole(pgoff_t pg_start, pgoff_t pg_end) {
  pgoff_t index;

  for (index = pg_start; index < pg_end; index++) {
    DnodeOfData dn;

    NodeManager::SetNewDnode(dn, this, nullptr, nullptr, 0);
    if (zx_status_t err = Vfs()->GetNodeManager().GetDnodeOfData(dn, index, kRdOnlyNode);
        err != ZX_OK) {
      if (err == ZX_ERR_NOT_FOUND)
        continue;
      return err;
    }

    if (dn.data_blkaddr != kNullAddr)
      TruncateDataBlocksRange(&dn, 1);
    F2fsPutDnode(&dn);
  }
  return ZX_OK;
}

void VnodeF2fs::TruncateToSize() {
  if (!(IsDir() || IsReg() || IsLink()))
    return;

  if (zx_status_t ret = TruncateBlocks(GetSize()); ret == ZX_OK) {
    timespec cur_time;
    clock_gettime(CLOCK_REALTIME, &cur_time);
    SetMTime(cur_time);
    SetCTime(cur_time);
  }
}

// Called at Recycle if nlink_ is zero
void VnodeF2fs::EvictVnode() {
  SuperblockInfo &superblock_info = Vfs()->GetSuperblockInfo();
  // [TODO] currently, there is no cache for node blocks
  // truncate_inode_pages(&inode->i_data, 0);

  if (ino_ == superblock_info.GetNodeIno() || ino_ == superblock_info.GetMetaIno())
    return;

  // BUG_ON(AtomicRead(&fi.->dirty_dents));
  // RemoveDirtyDirInode(this);

  if (GetNlink() || IsBad())
    return;

  SetFlag(InodeInfoFlag::kNoAlloc);
  SetSize(0);

  if (HasBlocks())
    TruncateToSize();

  {
    fs::SharedLock rlock(superblock_info.GetFsLock(LockType::kFileOp));
    Vfs()->GetNodeManager().RemoveInodePage(this);
  }
  Vfs()->EvictVnode(this);
  //  no_delete:
  //  ClearInode(this);
}

void VnodeF2fs::Init() {
  memset(&fi_, 0, sizeof(InodeInfo));
#if 0  // porting needed
  // AtomicSet(&vnode->fi.vfs_inode.i_version, 1);
#endif
  AtomicSet(&fi_.dirty_dents, 0);
  SetCurDirDepth(1);
  SetFlag(InodeInfoFlag::kInit);
  Activate();
}

void VnodeF2fs::MarkInodeDirty() {
  if (SetFlag(InodeInfoFlag::kDirty)) {
    return;
  }
  ZX_ASSERT(Vfs()->GetVCache().AddDirty(this) == ZX_OK);
}

void VnodeF2fs::Sync(SyncCallback closure) {
  SyncFile(0, GetSize(), 0);
  closure(ZX_OK);
}

zx_status_t VnodeF2fs::QueryFilesystem(fuchsia_io_admin::wire::FilesystemInfo *info) {
  SuperblockInfo &superblock_info = Vfs()->GetSuperblockInfo();
  *info = {};
  info->block_size = kBlockSize;
  info->max_filename_size = kMaxNameLen;
  info->fs_type = VFS_TYPE_F2FS;
  info->fs_id = Vfs()->GetFsIdLegacy();
  info->total_bytes = superblock_info.GetUserBlockCount() * kBlockSize;
  info->used_bytes = Vfs()->ValidUserBlocks() * kBlockSize;
  info->total_nodes = superblock_info.GetTotalNodeCount();
  info->used_nodes = superblock_info.GetTotalValidInodeCount();

  constexpr std::string_view kFsName = "f2fs";
  static_assert(kFsName.size() + 1 < fuchsia_io_admin::wire::kMaxFsNameBuffer,
                "f2fs name too long");
  info->name[kFsName.copy(reinterpret_cast<char *>(info->name.data()),
                          fuchsia_io_admin::wire::kMaxFsNameBuffer - 1)] = '\0';

  // TODO(unknown): Fill info->free_shared_pool_bytes using fvm info

  return ZX_OK;
}

zx_status_t VnodeF2fs::SyncFile(loff_t start, loff_t end, int datasync) {
  SuperblockInfo &superblock_info = Vfs()->GetSuperblockInfo();
  __UNUSED uint64_t cur_version;
  zx_status_t ret = 0;
  bool need_cp = false;
#if 0  // porting needed
  // WritebackControl wbc;

  // wbc.sync_mode = WB_SYNC_ALL;
  // wbc.nr_to_write = LONG_MAX;
  // wbc.for_reclaim = 0;

  // ret = filemap_write_and_wait_range(inode->i_mapping, start, end);
  // if (ret)
  //   return ret;
#endif

#if 0  // porting needed
  // if (inode->i_sb->s_flags & MS_RDONLY)
  //   goto out;
  // if (datasync && !(i_state & I_DIRTY_DATASYNC)) {
  //  goto out;
  //}
#endif

  {
    fbl::AutoLock cplock(&superblock_info.GetCheckpointMutex());
    cur_version = LeToCpu(superblock_info.GetCheckpoint().checkpoint_ver);
  }

#if 0  // porting needed
  // if (fi.data_version != cur_version &&
  //        !(i_state & I_DIRTY)) {
  //  goto out;
  //}
#endif
  fi_.data_version--;

  if (!IsReg() || GetNlink() != 1)
    need_cp = true;
  if (TestFlag(InodeInfoFlag::kNeedCp))
    need_cp = true;
  if (!Vfs()->SpaceForRollForward())
    need_cp = true;
  if (superblock_info.TestOpt(kMountDisableRollForward) || NeedToSyncDir())
    need_cp = true;

  // TODO: it intended to update cached page for this, but
  // the current impl. writes out inode in a sync manner
  // WriteInode(nullptr);

  if (need_cp) {
    // TODO: remove it after implementing cache
    WriteInode(nullptr);
    // all the dirty node pages should be flushed for POR
    ret = Vfs()->SyncFs(1);
    ClearFlag(InodeInfoFlag::kNeedCp);
  } else {
    // TODO: it intended to flush all dirty node pages on cache
    // remove it after cache
    Page *node_page = nullptr;
    int mark = !Vfs()->GetNodeManager().IsCheckpointedNode(Ino());
    if (ret = Vfs()->GetNodeManager().GetNodePage(Ino(), &node_page); ret != ZX_OK) {
      return ret;
    }

    NodeManager::SetFsyncMark(*node_page, 1);
    NodeManager::SetDentryMark(*node_page, mark);

    UpdateInode(node_page);
    F2fsPutPage(node_page, 1);

#if 0  // porting needed
    // while (Vfs()->GetNodeManager().SyncNodePages(Ino(), &wbc) == 0)
    //   WriteInode(nullptr);
    // filemap_fdatawait_range(nullptr,//superblock_info->node_inode->i_mapping,
    //           0, LONG_MAX);
#endif
  }
#if 0  // porting needed
  // out:
#endif
  return ret;
}

int VnodeF2fs::NeedToSyncDir() {
#if 0  // porting needed
  // dentry = d_find_any_alias(vnode);
  // if (!dentry) {
    //   // Iput(inode);
  //   return 0;
  // }
  // pino = dentry->d_parent->d_inode->i_ino;
  // dput(dentry);
  // Iput();
#endif
  ZX_ASSERT(GetParentNid() < kNullIno);
  return !Vfs()->GetNodeManager().IsCheckpointedNode(GetParentNid());
}

void VnodeF2fs::Notify(std::string_view name, unsigned event) { watcher_.Notify(name, event); }

zx_status_t VnodeF2fs::WatchDir(fs::Vfs *vfs, uint32_t mask, uint32_t options,
                                zx::channel watcher) {
  return watcher_.WatchDir(vfs, this, mask, options, std::move(watcher));
}

inline void VnodeF2fs::GetExtentInfo(const Extent &i_ext) {
  std::lock_guard lock(fi_.ext.ext_lock);
  fi_.ext.fofs = LeToCpu(i_ext.fofs);
  fi_.ext.blk_addr = LeToCpu(i_ext.blk_addr);
  fi_.ext.len = LeToCpu(i_ext.len);
}

inline void VnodeF2fs::SetRawExtent(Extent &i_ext) {
  fs::SharedLock lock(fi_.ext.ext_lock);
  i_ext.fofs = CpuToLe(static_cast<uint32_t>(fi_.ext.fofs));
  i_ext.blk_addr = CpuToLe(fi_.ext.blk_addr);
  i_ext.len = CpuToLe(fi_.ext.len);
}

}  // namespace f2fs
