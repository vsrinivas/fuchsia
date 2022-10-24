// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <sys/stat.h>

#include "src/storage/f2fs/f2fs.h"

namespace f2fs {

#ifdef __Fuchsia__
VnodeF2fs::VnodeF2fs(F2fs *fs, ino_t ino) : PagedVnode(*fs->vfs()), ino_(ino), fs_(fs) {}
#else   // __Fuchsia__
VnodeF2fs::VnodeF2fs(F2fs *fs, ino_t ino) : ino_(ino), fs_(fs) {}
#endif  // __Fuchsia__

fs::VnodeProtocolSet VnodeF2fs::GetProtocols() const {
  if (IsDir()) {
    return fs::VnodeProtocol::kDirectory;
  }
  return fs::VnodeProtocol::kFile;
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

bool VnodeF2fs::IsNode() {
  SuperblockInfo &superblock_info = fs()->GetSuperblockInfo();
  return ino_ == superblock_info.GetNodeIno();
}

bool VnodeF2fs::IsMeta() {
  SuperblockInfo &superblock_info = fs()->GetSuperblockInfo();
  return ino_ == superblock_info.GetMetaIno();
}

#ifdef __Fuchsia__
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

zx_status_t VnodeF2fs::GetVmo(fuchsia_io::wire::VmoFlags flags, zx::vmo *out_vmo) {
  if (flags & fuchsia_io::wire::VmoFlags::kExecute) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  if ((flags & fuchsia_io::wire::VmoFlags::kSharedBuffer) &&
      (flags & fuchsia_io::wire::VmoFlags::kWrite)) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  std::lock_guard lock(mutex_);
  ZX_DEBUG_ASSERT(open_count() > 0);

  if (!IsReg()) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  size_t rounded_size = fbl::round_up(size_, static_cast<size_t>(PAGE_SIZE));
  ZX_DEBUG_ASSERT(rounded_size >= size_);
  if (rounded_size == 0) {
    rounded_size = PAGE_SIZE;
  }

  if (auto status = CreatePagedVmo(rounded_size); status != ZX_OK) {
    return status;
  }

  return ClonePagedVmo(flags, rounded_size, out_vmo);
}

zx_status_t VnodeF2fs::CreatePagedVmo(size_t size) {
  if (!paged_vmo()) {
    if (auto status = EnsureCreatePagedVmo(size); status.is_error()) {
      return status.error_value();
    }
    SetPagedVmoName();
  } else {
    // TODO: Resize paged_vmo() if slice clone is available on a resizable VMO support.
    // It should not return an error because mmapped area can be smaller than file size.
    size_t vmo_size;
    paged_vmo().get_size(&vmo_size);
    if (size > vmo_size) {
      FX_LOGS(WARNING) << "Memory mapped VMO size may be smaller than the file size. (VMO size="
                       << vmo_size << ", File size=" << size << ")";
    }
  }
  return ZX_OK;
}

void VnodeF2fs::SetPagedVmoName() {
  fbl::StringBuffer<ZX_MAX_NAME_LEN> name;
  name.Clear();
  name.AppendPrintf("%s-%.8s", "f2fs", GetNameView().data());
  paged_vmo().set_property(ZX_PROP_NAME, name.data(), name.size());
}

zx_status_t VnodeF2fs::ClonePagedVmo(fuchsia_io::wire::VmoFlags flags, size_t size,
                                     zx::vmo *out_vmo) {
  if (!paged_vmo()) {
    return ZX_ERR_NOT_FOUND;
  }

  zx_rights_t rights = ZX_RIGHTS_BASIC | ZX_RIGHT_MAP | ZX_RIGHTS_PROPERTY;
  rights |= (flags & fuchsia_io::wire::VmoFlags::kRead) ? ZX_RIGHT_READ : 0;
  rights |= (flags & fuchsia_io::wire::VmoFlags::kWrite) ? ZX_RIGHT_WRITE : 0;

  uint32_t options;
  if (flags & fuchsia_io::wire::VmoFlags::kSharedBuffer) {
    options = ZX_VMO_CHILD_SLICE;
  } else {
    options = ZX_VMO_CHILD_SNAPSHOT_AT_LEAST_ON_WRITE;
  }
  if (!(flags & fuchsia_io::wire::VmoFlags::kWrite)) {
    options |= ZX_VMO_CHILD_NO_WRITE;
  }

  zx::vmo clone;
  size_t clone_size = 0;
  paged_vmo().get_size(&clone_size);
  if (auto status = paged_vmo().create_child(options, 0, clone_size, &clone); status != ZX_OK) {
    FX_LOGS(ERROR) << "Faild to create child VMO" << zx_status_get_string(status);
    return status;
  }
  DidClonePagedVmo();

  if (auto status = clone.replace(rights, &clone); status != ZX_OK) {
    return status;
  }

  *out_vmo = std::move(clone);
  return ZX_OK;
}

void VnodeF2fs::VmoRead(uint64_t offset, uint64_t length) {
  ZX_DEBUG_ASSERT(kBlockSize == PAGE_SIZE);
  ZX_DEBUG_ASSERT(offset % kBlockSize == 0);
  ZX_DEBUG_ASSERT(length);
  ZX_DEBUG_ASSERT(length % kBlockSize == 0);

  // It tries to create and populate a vmo with locked Pages first.
  // The mmap flag for the Pages will be cleared in RecycleNode() when there is no reference to
  // |this|.
  auto vmo_or = PopulateAndGetMmappedVmo(offset, length);
  fs::SharedLock rlock(mutex_);
  if (!paged_vmo()) {
    // Races with calling FreePagedVmo() on another thread can result in stale read requests. Ignore
    // them if the VMO is gone.
    FX_LOGS(WARNING) << "A pager-backed VMO is already freed: " << ZX_ERR_NOT_FOUND;
    return;
  }

  if (vmo_or.is_error()) {
    FX_LOGS(ERROR) << "Failed to read a VMO at " << offset << " + " << length << ", "
                   << vmo_or.status_string();
    ReportPagerError(offset, length, ZX_ERR_BAD_STATE);
    return;
  }

  std::optional vfs = this->vfs();
  ZX_ASSERT(vfs.has_value());
  if (auto ret =
          vfs.value().get().SupplyPages(paged_vmo(), offset, length, std::move(vmo_or.value()), 0);
      ret.is_error()) {
    FX_LOGS(ERROR) << "Failed to supply a VMO to " << offset << " + " << length << ", "
                   << vmo_or.status_string();
    ReportPagerError(offset, length, ZX_ERR_BAD_STATE);
  }
}

zx::result<zx::vmo> VnodeF2fs::PopulateAndGetMmappedVmo(const size_t offset, const size_t length) {
  zx::vmo vmo;
  // It creates a zero-filled vmo, so we don't need to fill an invalidated area with zero.
  if (auto status = vmo.create(length, 0, &vmo); status != ZX_OK) {
    return zx::error(status);
  }

  // Populate |vmo| from its node block if it has inline data.
  if (TestFlag(InodeInfoFlag::kInlineData)) {
    if (auto ret = PopulateVmoWithInlineData(vmo); ret.is_error()) {
      return zx::error(ret.status_value());
    }
    return zx::ok(std::move(vmo));
  }

  pgoff_t block_index = safemath::CheckDiv<pgoff_t>(offset, kBlockSize).ValueOrDie();
  for (size_t copied_bytes = 0; copied_bytes < length; copied_bytes += kBlockSize, ++block_index) {
    LockedPage data_page;
    if (zx_status_t status = GetLockedDataPage(block_index, &data_page); status != ZX_OK) {
      // If |data_page| is not a valid Page, just grab one.
      if (status = GrabCachePage(block_index, &data_page); status != ZX_OK) {
        return zx::error(status);
      }
      // Just set a mmapped flag for an invalid page.
      ZX_DEBUG_ASSERT(!data_page->IsUptodate());
      data_page->SetMmapped();
    } else {
      data_page->SetMmapped();
      // If it is a valid Page, fill |vmo| with |data_page|.
      if (data_page->IsUptodate()) {
        vmo.write(data_page->GetAddress(), copied_bytes, kBlockSize);
      }
    }
  }
  return zx::ok(std::move(vmo));
}

void VnodeF2fs::OnNoPagedVmoClones() {
  // Override PagedVnode::OnNoPagedVmoClones().
  // We intend to keep PagedVnode::paged_vmo alive while this vnode has any reference. Here, we just
  // set a ZX_VMO_OP_DONT_NEED hint to allow mm to reclaim the committed pages when there is no
  // clone. This way can avoid a race condition between page fault and paged_vmo release.
  ZX_DEBUG_ASSERT(!has_clones());
  size_t vmo_size;
  paged_vmo().get_size(&vmo_size);
  zx_status_t status = paged_vmo().op_range(ZX_VMO_OP_DONT_NEED, 0, vmo_size, nullptr, 0);
  if (status != ZX_OK) {
    FX_LOGS(WARNING) << "Hinting DONT_NEED on f2fs failed: " << zx_status_get_string(status);
  }
}

void VnodeF2fs::ReportPagerError(const uint64_t offset, const uint64_t length,
                                 const zx_status_t err) {
  std::optional vfs = this->vfs();
  ZX_ASSERT(vfs.has_value());
  if (auto result = vfs.value().get().ReportPagerError(paged_vmo(), offset, length, err);
      result.is_error()) {
    FX_LOGS(ERROR) << "Failed to report pager error to kernel: " << result.status_string();
  }
}
#endif  // __Fuchsia__

zx_status_t VnodeF2fs::InvalidatePagedVmo(uint64_t offset, size_t len) {
  zx_status_t ret = ZX_OK;
#ifdef __Fuchsia__
  fs::SharedLock rlock(mutex_);
  if (paged_vmo()) {
    ret = paged_vmo().op_range(ZX_VMO_OP_ZERO, offset, len, nullptr, 0);
  }
#endif  // __Fuchsia
  return ret;
}

zx_status_t VnodeF2fs::WritePagedVmo(const void *buffer_address, uint64_t offset, size_t len) {
  zx_status_t ret = ZX_OK;
#ifdef __Fuchsia__
  fs::SharedLock rlock(mutex_);
  if (paged_vmo()) {
    ret = paged_vmo().write(buffer_address, offset, len);
  }
#endif  // __Fuchsia
  return ret;
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
  if (ino == fs->GetSuperblockInfo().GetNodeIno() || ino == fs->GetSuperblockInfo().GetMetaIno()) {
    *out = fbl::MakeRefCounted<VnodeF2fs>(fs, ino);
    return ZX_OK;
  }

  /* Check if ino is within scope */
  fs->GetNodeManager().CheckNidRange(ino);

  LockedPage node_page;
  if (fs->GetNodeManager().GetNodePage(ino, &node_page) != ZX_OK) {
    return ZX_ERR_NOT_FOUND;
  }

  Node *rn = node_page->GetAddress<Node>();
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
  // Don't count the in-memory inode.i_blocks for compatibility with the generic filesystem
  // including linux f2fs.
  vnode->SetBlocks(safemath::CheckSub<uint64_t>(LeToCpu(ri.i_blocks), 1).ValueOrDie());
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
    failed.reset();
    out = nullptr;
    return ZX_ERR_NOT_FOUND;
  }

  vnode->SetName(name);

  if (ri.i_inline & kInlineDentry) {
    vnode->SetFlag(InodeInfoFlag::kInlineDentry);
    vnode->SetInlineXattrAddrs(kInlineXattrAddrs);
  }
  if (ri.i_inline & kInlineData) {
    vnode->SetFlag(InodeInfoFlag::kInlineData);
  }
  if (ri.i_inline & kInlineXattr) {
    vnode->SetFlag(InodeInfoFlag::kInlineXattr);
    vnode->SetInlineXattrAddrs(kInlineXattrAddrs);
  }
  if (ri.i_inline & kExtraAttr) {
    vnode->SetExtraISize(ri.i_extra_isize);
    if (ri.i_inline & kInlineXattr) {
      vnode->SetInlineXattrAddrs(ri.i_inline_xattr_size);
    }
  }
  if (ri.i_inline & kDataExist) {
    vnode->SetFlag(InodeInfoFlag::kDataExist);
  }

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
    ZX_ASSERT_MSG(open_count() == 0, "RecycleNode[%s:%u]: open_count must be zero (%lu)",
                  GetNameView().data(), GetKey(), open_count());
  }
  ReleasePagedVmo();
  if (GetNlink()) {
    // f2fs removes the last reference to a dirty vnode from the dirty vnode list
    // when there is no dirty Page for the vnode at checkpoint time.
    if (GetDirtyPageCount()) {
      // It can happen only when CpFlag::kCpErrorFlag is set.
      FX_LOGS(WARNING) << "RecycleNode[" << GetNameView().data() << ":" << GetKey()
                       << "]: GetDirtyPageCount() must be zero but " << GetDirtyPageCount()
                       << ". CpFlag::kCpErrorFlag is "
                       << (fs()->GetSuperblockInfo().TestCpFlags(CpFlag::kCpErrorFlag)
                               ? "set."
                               : "not set.");
    }
    file_cache_.Reset();
#ifdef __Fuchsia__
    vmo_manager_.Reset();
#endif  // __Fuchsia__
    fs()->GetVCache().Downgrade(this);
  } else {
    if (!fs()->IsTearDown()) {
      // This vnode is an orphan file deleted in PagedVfs::Teardown().
      // Purge it at the next mount time.
      EvictVnode();
    }
    Deactivate();
    file_cache_.Reset();
#ifdef __Fuchsia__
    vmo_manager_.Reset();
#endif  // __Fuchsia__
    delete this;
  }
}

zx_status_t VnodeF2fs::GetAttributes(fs::VnodeAttributes *a) {
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
//     Vget(fs(), ino, &vnode_refptr);
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

  if (ino != fs->GetSuperblockInfo().GetNodeIno() && ino != fs->GetSuperblockInfo().GetMetaIno()) {
    if (!fs->GetSuperblockInfo().IsOnRecovery() && vnode_refptr->GetNlink() == 0) {
      vnode_refptr->SetFlag(InodeInfoFlag::kBad);
      vnode_refptr.reset();
      *out = nullptr;
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

  node_page->WaitOnWriteback();

  rn = node_page->GetAddress<Node>();
  ri = &(rn->i);

  ri->i_mode = CpuToLe(GetMode());
  ri->i_advise = GetAdvise();
  ri->i_uid = CpuToLe(GetUid());
  ri->i_gid = CpuToLe(GetGid());
  ri->i_links = CpuToLe(GetNlink());
  ri->i_size = CpuToLe(GetSize());
  // For on-disk i_blocks, we keep counting inode block for backward compatibility.
  ri->i_blocks = CpuToLe(safemath::CheckAdd<uint64_t>(GetBlocks(), 1).ValueOrDie());
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
  ri->i_pino = CpuToLe(GetParentNid());
  ri->i_generation = CpuToLe(GetGeneration());
  ri->i_dir_level = GetDirLevel();

  std::string_view name = GetNameView();
  // double check |name|
  ZX_DEBUG_ASSERT(IsValidNameLength(name));
  auto size = safemath::checked_cast<uint32_t>(name.size());
  ri->i_namelen = CpuToLe(size);
  name.copy(reinterpret_cast<char *>(&ri->i_name[0]), size);

  if (TestFlag(InodeInfoFlag::kInlineData)) {
    ri->i_inline |= kInlineData;
  } else {
    ri->i_inline &= ~kInlineData;
  }
  if (TestFlag(InodeInfoFlag::kInlineDentry)) {
    ri->i_inline |= kInlineDentry;
  } else {
    ri->i_inline &= ~kInlineDentry;
  }
  if (GetExtraISize()) {
    ri->i_inline |= kExtraAttr;
    ri->i_extra_isize = GetExtraISize();
    if (TestFlag(InodeInfoFlag::kInlineXattr)) {
      ri->i_inline_xattr_size = GetInlineXattrAddrs();
    }
  }
  if (TestFlag(InodeInfoFlag::kDataExist)) {
    ri->i_inline |= kDataExist;
  } else {
    ri->i_inline &= ~kDataExist;
  }
  if (TestFlag(InodeInfoFlag::kInlineXattr)) {
    ri->i_inline |= kInlineXattr;
  } else {
    ri->i_inline &= ~kInlineXattr;
  }

  node_page->SetDirty();
}

zx_status_t VnodeF2fs::WriteInode(bool is_reclaim) {
  SuperblockInfo &superblock_info = fs()->GetSuperblockInfo();
  zx_status_t ret = ZX_OK;

  if (ino_ == superblock_info.GetNodeIno() || ino_ == superblock_info.GetMetaIno()) {
    return ret;
  }

  if (IsDirty()) {
    fs::SharedLock rlock(superblock_info.GetFsLock(LockType::kNodeOp));
    LockedPage node_page;
    if (ret = fs()->GetNodeManager().GetNodePage(ino_, &node_page); ret != ZX_OK) {
      return ret;
    }
    UpdateInode(node_page.get());
  }

  return ZX_OK;
}

zx_status_t VnodeF2fs::DoTruncate(size_t len) {
  zx_status_t ret;

  if (ret = TruncateBlocks(len); ret == ZX_OK) {
    SetSize(len);
    if (GetSize() == 0) {
      ClearFlag(InodeInfoFlag::kDataExist);
    }

    timespec cur_time;
    clock_gettime(CLOCK_REALTIME, &cur_time);
    SetCTime(cur_time);
    SetMTime(cur_time);
    MarkInodeDirty();
  }

  fs()->GetSegmentManager().BalanceFs();
  return ret;
}

int VnodeF2fs::TruncateDataBlocksRange(NodePage &node_page, uint32_t ofs_in_node, uint32_t count) {
  int nr_free = 0;
  Node *raw_node = node_page.GetAddress<Node>();

  for (; count > 0; --count, ++ofs_in_node) {
    uint32_t *addr = BlkaddrInNode(*raw_node) + ofs_in_node;
    block_t blkaddr = LeToCpu(*addr);
    if (blkaddr == kNullAddr)
      continue;
    SetDataBlkaddr(node_page, ofs_in_node, kNullAddr);
    UpdateExtentCache(kNullAddr, node_page.StartBidxOfNode(*this) + ofs_in_node);
    fs()->GetSegmentManager().InvalidateBlocks(blkaddr);
    fs()->DecValidBlockCount(this, 1);
    ++nr_free;
  }
  if (nr_free) {
    node_page.SetDirty();
    MarkInodeDirty();
  }
  return nr_free;
}

void VnodeF2fs::TruncateDataBlocks(NodePage &node_page) {
  TruncateDataBlocksRange(node_page, 0, kAddrsPerBlock);
}

void VnodeF2fs::TruncatePartialDataPage(uint64_t from) {
  size_t offset = from & (kPageSize - 1);
  fbl::RefPtr<Page> page;

  if (!offset)
    return;

  if (FindDataPage(from >> kPageCacheShift, &page) != ZX_OK)
    return;

  LockedPage locked_page(page);
  locked_page->WaitOnWriteback();
  locked_page->ZeroUserSegment(static_cast<uint32_t>(offset), kPageSize);
  locked_page->SetDirty();

  if (locked_page->IsMmapped()) {
    ZX_ASSERT(WritePagedVmo(locked_page->GetAddress(), (from >> kPageCacheShift) * kBlockSize,
                            kBlockSize) == ZX_OK);
  }
}

zx_status_t VnodeF2fs::TruncateBlocks(uint64_t from) {
  SuperblockInfo &superblock_info = fs()->GetSuperblockInfo();
  uint32_t blocksize = superblock_info.GetBlocksize();
  int count = 0;
  zx_status_t err;

  if (from > GetSize())
    return ZX_OK;

  pgoff_t free_from =
      static_cast<pgoff_t>((from + blocksize - 1) >> (superblock_info.GetLogBlocksize()));

  {
    fs::SharedLock rlock(superblock_info.GetFsLock(LockType::kFileOp));
    auto locked_data_pages = InvalidatePages(free_from);

    do {
      LockedPage node_page;
      err = fs()->GetNodeManager().FindLockedDnodePage(*this, free_from, &node_page);
      if (err) {
        if (err == ZX_ERR_NOT_FOUND)
          break;
        return err;
      }

      if (IsInode(*node_page)) {
        count = GetAddrsPerInode();
      } else {
        count = kAddrsPerBlock;
      }

      uint32_t ofs_in_node;
      if (auto result = fs()->GetNodeManager().GetOfsInDnode(*this, free_from); result.is_error()) {
        return result.error_value();
      } else {
        ofs_in_node = result.value();
      }
      count -= ofs_in_node;
      ZX_ASSERT(count >= 0);

      if (ofs_in_node || IsInode(*node_page)) {
        TruncateDataBlocksRange(node_page.GetPage<NodePage>(), ofs_in_node, count);
        free_from += count;
      }
    } while (false);

    err = fs()->GetNodeManager().TruncateInodeBlocks(*this, free_from);
  }
  // lastly zero out the first data page
  TruncatePartialDataPage(from);

  return err;
}

zx_status_t VnodeF2fs::TruncateHole(pgoff_t pg_start, pgoff_t pg_end) {
  std::vector<LockedPage> locked_data_pages = InvalidatePages(pg_start, pg_end);
  for (pgoff_t index = pg_start; index < pg_end; ++index) {
    LockedPage dnode_page;
    if (zx_status_t err = fs()->GetNodeManager().GetLockedDnodePage(*this, index, &dnode_page);
        err != ZX_OK) {
      if (err == ZX_ERR_NOT_FOUND) {
        continue;
      }
      return err;
    }

    uint32_t ofs_in_dnode;
    if (auto result = fs()->GetNodeManager().GetOfsInDnode(*this, index); result.is_error()) {
      if (result.error_value() == ZX_ERR_NOT_FOUND) {
        continue;
      }
      return result.error_value();
    } else {
      ofs_in_dnode = result.value();
    }

    if (DatablockAddr(&dnode_page.GetPage<NodePage>(), ofs_in_dnode) != kNullAddr) {
      TruncateDataBlocksRange(dnode_page.GetPage<NodePage>(), ofs_in_dnode, 1);
    }
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

void VnodeF2fs::ReleasePagedVmo() {
  zx::result<bool> valid_vmo_or;
  {
    std::lock_guard lock(mutex_);
    valid_vmo_or = ReleasePagedVmoUnsafe();
    ZX_DEBUG_ASSERT(valid_vmo_or.is_ok());
  }
  // If necessary, clear the mmap flag in its node Page after releasing its paged VMO.
  if (valid_vmo_or.value() && TestFlag(InodeInfoFlag::kInlineData)) {
    LockedPage inline_page;
    if (zx_status_t ret = fs()->GetNodeManager().GetNodePage(Ino(), &inline_page); ret == ZX_OK) {
      inline_page->ClearMmapped();
    } else {
      FX_LOGS(WARNING) << "Failed to get the inline data page. " << ret;
    }
  }
}

zx::result<bool> VnodeF2fs::ReleasePagedVmoUnsafe() {
#ifdef __Fuchsia__
  if (paged_vmo()) {
    fbl::RefPtr<fs::Vnode> pager_reference = FreePagedVmo();
    ZX_DEBUG_ASSERT(!pager_reference);
    return zx::ok(true);
  }
#endif
  return zx::ok(false);
}

// Called at Recycle if nlink_ is zero
void VnodeF2fs::EvictVnode() {
  SuperblockInfo &superblock_info = fs()->GetSuperblockInfo();

  if (ino_ == superblock_info.GetNodeIno() || ino_ == superblock_info.GetMetaIno())
    return;

  if (GetNlink() || IsBad())
    return;

  SetFlag(InodeInfoFlag::kNoAlloc);
  SetSize(0);

  if (HasBlocks())
    TruncateToSize();

  {
    fs::SharedLock rlock(superblock_info.GetFsLock(LockType::kFileOp));
    fs()->GetNodeManager().RemoveInodePage(this);
    ZX_ASSERT(GetDirtyPageCount() == 0);
  }
  fs()->EvictVnode(this);
}

void VnodeF2fs::Init() {
  SetCurDirDepth(1);
  SetFlag(InodeInfoFlag::kInit);
  Activate();
}

void VnodeF2fs::MarkInodeDirty() {
  if (SetFlag(InodeInfoFlag::kDirty)) {
    return;
  }
  if (IsNode() || IsMeta()) {
    return;
  }
  if (!GetNlink()) {
    return;
  }
  ZX_ASSERT(fs()->GetVCache().AddDirty(this) == ZX_OK);
}

#ifdef __Fuchsia__
void VnodeF2fs::Sync(SyncCallback closure) {
  closure(SyncFile(0, safemath::checked_cast<loff_t>(GetSize()), 0));
}
#endif  // __Fuchsia__

bool VnodeF2fs::NeedDoCheckpoint() {
  if (!IsReg()) {
    return true;
  }
  if (GetNlink() != 1) {
    return true;
  }
  if (TestFlag(InodeInfoFlag::kNeedCp)) {
    return true;
  }
  if (!fs()->SpaceForRollForward()) {
    return true;
  }
  if (NeedToSyncDir()) {
    return true;
  }
  if (fs()->GetSuperblockInfo().TestOpt(kMountDisableRollForward)) {
    return true;
  }
  if (fs()->GetSuperblockInfo().FindVnodeFromVnodeSet(InoType::kModifiedDirIno, GetParentNid())) {
    return true;
  }
  return false;
}

zx_status_t VnodeF2fs::SyncFile(loff_t start, loff_t end, int datasync) {
  // When kCpErrorFlag is set, write is not allowed.
  if (fs()->GetSuperblockInfo().TestCpFlags(CpFlag::kCpErrorFlag)) {
    return ZX_ERR_BAD_STATE;
  }

  // TODO: When fdatasync is available, check if it should be written.
  if (!IsDirty()) {
    return ZX_OK;
  }

  // Write out dirty data pages and wait for completion
  WritebackOperation op = {.bSync = true};
  Writeback(op);

  // TODO: STRICT mode will be supported when FUA interface is added.
  // Currently, only POSIX mode is supported.
  // TODO: We should consider fdatasync for WriteInode().
  WriteInode(false);
  bool need_cp = NeedDoCheckpoint();

  if (need_cp) {
    fs()->SyncFs();
    ClearFlag(InodeInfoFlag::kNeedCp);
    // Check if checkpoint errors happen during fsync().
    if (fs()->GetSuperblockInfo().TestCpFlags(CpFlag::kCpErrorFlag)) {
      return ZX_ERR_BAD_STATE;
    }
  } else {
    // Write dnode pages
    fs()->GetNodeManager().FsyncNodePages(*this);
    fs()->GetBc().Flush();

    // TODO: Add flags to log recovery information to NAT entries and decide whether to write inode
    // or not.
  }
  return ZX_OK;
}

bool VnodeF2fs::NeedToSyncDir() {
  ZX_ASSERT(GetParentNid() < kNullIno);
  return !fs()->GetNodeManager().IsCheckpointedNode(GetParentNid());
}

#ifdef __Fuchsia__
void VnodeF2fs::Notify(std::string_view name, fuchsia_io::wire::WatchEvent event) {
  watcher_.Notify(name, event);
}

zx_status_t VnodeF2fs::WatchDir(fs::Vfs *vfs, fuchsia_io::wire::WatchMask mask, uint32_t options,
                                fidl::ServerEnd<fuchsia_io::DirectoryWatcher> watcher) {
  return watcher_.WatchDir(vfs, this, mask, options, std::move(watcher));
}
#endif  // __Fuchsia__

void VnodeF2fs::GetExtentInfo(const Extent &i_ext) {
  std::lock_guard lock(fi_.ext.ext_lock);
  fi_.ext.fofs = LeToCpu(i_ext.fofs);
  fi_.ext.blk_addr = LeToCpu(i_ext.blk_addr);
  fi_.ext.len = LeToCpu(i_ext.len);
}

void VnodeF2fs::SetRawExtent(Extent &i_ext) {
  fs::SharedLock lock(fi_.ext.ext_lock);
  i_ext.fofs = CpuToLe(static_cast<uint32_t>(fi_.ext.fofs));
  i_ext.blk_addr = CpuToLe(fi_.ext.blk_addr);
  i_ext.len = CpuToLe(fi_.ext.len);
}

void VnodeF2fs::UpdateVersion() {
  fi_.data_version = LeToCpu(fs()->GetSuperblockInfo().GetCheckpoint().checkpoint_ver);
}

}  // namespace f2fs
