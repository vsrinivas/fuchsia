// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <sys/stat.h>

#include "src/storage/f2fs/f2fs.h"

#ifndef __Fuchsia__
#include "lib/stdcompat/string_view.h"
#endif  // __Fuchsia__

namespace f2fs {

zx_status_t Dir::NewInode(uint32_t mode, fbl::RefPtr<VnodeF2fs> *out) {
  SuperblockInfo &superblock_info = fs()->GetSuperblockInfo();
  nid_t ino;
  fbl::RefPtr<VnodeF2fs> vnode_refptr;
  VnodeF2fs *vnode = nullptr;

  do {
    fs::SharedLock rlock(superblock_info.GetFsLock(LockType::kFileOp));
    if (!fs()->GetNodeManager().AllocNid(ino)) {
      return ZX_ERR_NO_SPACE;
    }
  } while (false);

  VnodeF2fs::Allocate(fs(), ino, mode, &vnode_refptr);

  vnode = vnode_refptr.get();

  vnode->SetUid(getuid());

  if (HasGid()) {
    vnode->SetGid(GetGid());
    if (S_ISDIR(mode))
      mode |= S_ISGID;
  } else {
    vnode->SetGid(getgid());
  }

  vnode->SetMode(static_cast<umode_t>(mode));
  vnode->InitSize();
  vnode->InitNlink();
  vnode->InitBlocks();

  timespec cur_time;
  clock_gettime(CLOCK_REALTIME, &cur_time);
  vnode->SetATime(cur_time);
  vnode->SetCTime(cur_time);
  vnode->SetMTime(cur_time);
  vnode->SetGeneration(superblock_info.GetNextGeneration());
  superblock_info.IncNextGeneration();

  if (superblock_info.TestOpt(kMountInlineData) && !vnode->IsDir()) {
    vnode->SetFlag(InodeInfoFlag::kInlineData);
  }

  if (superblock_info.TestOpt(kMountInlineDentry) && vnode->IsDir()) {
    vnode->SetFlag(InodeInfoFlag::kInlineDentry);
    vnode->SetInlineXattrAddrs(kInlineXattrAddrs);
  }

  vnode->SetFlag(InodeInfoFlag::kNewInode);
  fs()->InsertVnode(vnode);
  vnode->MarkInodeDirty();

  *out = std::move(vnode_refptr);
  return ZX_OK;
}

bool Dir::IsMultimediaFile(VnodeF2fs &vnode, std::string_view sub) {
  // compare lower case
  if (cpp20::ends_with(vnode.GetNameView(), sub))
    return true;

  // compare upper case
  std::string upper_sub(sub);
  std::transform(upper_sub.cbegin(), upper_sub.cend(), upper_sub.begin(), ::toupper);
  return cpp20::ends_with(vnode.GetNameView(), upper_sub.c_str());
}

/**
 * Set multimedia files as cold files for hot/cold data separation
 */
void Dir::SetColdFile(VnodeF2fs &vnode) {
  const std::vector<std::string> &extension_list = fs()->GetSuperblockInfo().GetExtensionList();

  for (const auto &extension : extension_list) {
    if (IsMultimediaFile(vnode, extension)) {
      vnode.SetAdvise(FAdvise::kCold);
      break;
    }
  }
}

zx_status_t Dir::DoCreate(std::string_view name, uint32_t mode, fbl::RefPtr<fs::Vnode> *out) {
  SuperblockInfo &superblock_info = fs()->GetSuperblockInfo();
  fbl::RefPtr<VnodeF2fs> vnode_refptr;
  VnodeF2fs *vnode = nullptr;

  if (zx_status_t err = NewInode(S_IFREG | mode, &vnode_refptr); err != ZX_OK)
    return err;
  vnode = vnode_refptr.get();

  vnode->SetName(name);

  if (!superblock_info.TestOpt(kMountDisableExtIdentify))
    SetColdFile(*vnode);

  {
    fs::SharedLock rlock(superblock_info.GetFsLock(LockType::kFileOp));
    if (zx_status_t err = AddLink(name, vnode); err != ZX_OK) {
      vnode->ClearNlink();
      vnode->UnlockNewInode();
      fs()->GetVCache().RemoveDirty(vnode);
      fs()->GetNodeManager().AllocNidFailed(vnode->Ino());
      return err;
    }
  }

  fs()->GetNodeManager().AllocNidDone(vnode->Ino());

  vnode->UnlockNewInode();

  *out = std::move(vnode_refptr);
  return ZX_OK;
}

zx::result<> Dir::RecoverLink(VnodeF2fs &vnode) {
  std::lock_guard dir_lock(dir_mutex_);
  fbl::RefPtr<Page> page;
  auto dir_entry = FindEntry(vnode.GetNameView(), &page);
  if (dir_entry == nullptr) {
    AddLink(vnode.GetNameView(), &vnode);
  } else if (dir_entry && vnode.Ino() != LeToCpu(dir_entry->ino)) {
    // Remove old dentry
    fbl::RefPtr<VnodeF2fs> old_vnode_refptr;
    if (zx_status_t err = VnodeF2fs::Vget(fs(), dir_entry->ino, &old_vnode_refptr); err != ZX_OK) {
      return zx::error(err);
    }
    DeleteEntry(dir_entry, page, old_vnode_refptr.get());
    ZX_ASSERT(FindEntry(vnode.GetNameView()).status_value() == ZX_ERR_NOT_FOUND);
    AddLink(vnode.GetNameView(), &vnode);
  }
  return zx::ok();
}

zx_status_t Dir::Link(std::string_view name, fbl::RefPtr<fs::Vnode> new_child) {
  if (fs()->GetSuperblockInfo().TestCpFlags(CpFlag::kCpErrorFlag)) {
    return ZX_ERR_BAD_STATE;
  }

  if (!fs::IsValidName(name)) {
    return ZX_ERR_INVALID_ARGS;
  }

  fbl::RefPtr<VnodeF2fs> target = fbl::RefPtr<VnodeF2fs>::Downcast(std::move(new_child));
  if (target->IsDir()) {
    return ZX_ERR_NOT_FILE;
  }

  {
    std::lock_guard dir_lock(dir_mutex_);
    if (auto old_entry = FindEntry(name); !old_entry.is_error()) {
      return ZX_ERR_ALREADY_EXISTS;
    }

    timespec cur_time;
    clock_gettime(CLOCK_REALTIME, &cur_time);
    target->SetCTime(cur_time);

    {
      fs::SharedLock rlock(fs()->GetSuperblockInfo().GetFsLock(LockType::kFileOp));
      target->SetFlag(InodeInfoFlag::kIncLink);
      if (zx_status_t err = AddLink(name, target.get()); err != ZX_OK) {
        target->ClearFlag(InodeInfoFlag::kIncLink);
        return err;
      }
    }
  }

  fs()->GetSegmentManager().BalanceFs();

  return ZX_OK;
}

zx_status_t Dir::DoLookup(std::string_view name, fbl::RefPtr<fs::Vnode> *out) {
  fbl::RefPtr<VnodeF2fs> vn;

  if (!fs::IsValidName(name)) {
    return ZX_ERR_INVALID_ARGS;
  }

  if (auto dir_entry = FindEntry(name); !dir_entry.is_error()) {
    nid_t ino = LeToCpu((*dir_entry).ino);
    if (zx_status_t ret = VnodeF2fs::Vget(fs(), ino, &vn); ret != ZX_OK)
      return ret;

    *out = std::move(vn);

    return ZX_OK;
  }

  return ZX_ERR_NOT_FOUND;
}

zx_status_t Dir::Lookup(std::string_view name, fbl::RefPtr<fs::Vnode> *out) {
  fs::SharedLock dir_read_lock(dir_mutex_);
  return DoLookup(name, out);
}

zx_status_t Dir::DoUnlink(VnodeF2fs *vnode, std::string_view name) {
  DirEntry *de;
  {
    fbl::RefPtr<Page> page;

    de = FindEntry(name, &page);
    if (de == nullptr) {
      return ZX_ERR_NOT_FOUND;
    }

    {
      fs::SharedLock rlock(fs()->GetSuperblockInfo().GetFsLock(LockType::kFileOp));
      if (zx_status_t err = fs()->CheckOrphanSpace(); err != ZX_OK) {
        return err;
      }

      DeleteEntry(de, page, vnode);
    }
  }
  return ZX_OK;
}

#if 0  // porting needed
// int Dir::F2fsSymlink(dentry *dentry, const char *symname) {
//   return 0;
//   //   fbl::RefPtr<VnodeF2fs> vnode_refptr;
//   //   VnodeF2fs *vnode = nullptr;
//   //   unsigned symlen = strlen(symname) + 1;
//   //   int err;

//   //   err = NewInode(S_IFLNK | S_IRWXUGO, &vnode_refptr);
//   //   if (err)
//   //     return err;
//   //   vnode = vnode_refptr.get();

//   //   // inode->i_mapping->a_ops = &f2fs_dblock_aops;

//   //   // err = AddLink(dentry, vnode);
//   //   if (err)
//   //     goto out;

//   //   err = page_symlink(vnode, symname, symlen);
//   //   fs()->GetNodeManager().AllocNidDone(vnode->Ino());

//   //   // d_instantiate(dentry, vnode);
//   //   UnlockNewInode(vnode);

//   //   fs()->GetSegmentManager().BalanceFs();

//   //   return err;
//   // out:
//   //   vnode->ClearNlink();
//   //   UnlockNewInode(vnode);
//   //   fs()->GetNodeManager().AllocNidFailed(vnode->Ino());
//   //   return err;
// }
#endif

zx_status_t Dir::Mkdir(std::string_view name, uint32_t mode, fbl::RefPtr<fs::Vnode> *out) {
  fbl::RefPtr<VnodeF2fs> vnode_refptr;
  VnodeF2fs *vnode = nullptr;

  if (zx_status_t err = NewInode(S_IFDIR | mode, &vnode_refptr); err != ZX_OK)
    return err;
  vnode = vnode_refptr.get();
  vnode->SetName(name);
  vnode->SetFlag(InodeInfoFlag::kIncLink);
  {
    SuperblockInfo &superblock_info = fs()->GetSuperblockInfo();
    fs::SharedLock rlock(superblock_info.GetFsLock(LockType::kFileOp));
    if (zx_status_t err = AddLink(name, vnode); err != ZX_OK) {
      vnode->ClearFlag(InodeInfoFlag::kIncLink);
      vnode->ClearNlink();
      vnode->UnlockNewInode();
      fs()->GetVCache().RemoveDirty(vnode);
      fs()->GetNodeManager().AllocNidFailed(vnode->Ino());
      return err;
    }
  }
  fs()->GetNodeManager().AllocNidDone(vnode->Ino());
  vnode->UnlockNewInode();

  *out = std::move(vnode_refptr);
  return ZX_OK;
}

zx_status_t Dir::Rmdir(Dir *vnode, std::string_view name) {
  if (vnode->IsEmptyDir()) {
    return DoUnlink(vnode, name);
  }
  return ZX_ERR_NOT_EMPTY;
}

#if 0  // porting needed
// int Dir::F2fsMknod(dentry *dentry, umode_t mode, dev_t rdev) {
//   fbl::RefPtr<VnodeF2fs> vnode_refptr;
//   VnodeF2fs *vnode = nullptr;
//   int err = 0;

//   // if (!new_valid_dev(rdev))
//   //   return -EINVAL;

//   err = NewInode(mode, &vnode_refptr);
//   if (err)
//     return err;
//   vnode = vnode_refptr.get();

//   // init_special_inode(inode, inode->i_mode, rdev);
//   // inode->i_op = &f2fs_special_inode_operations;

//   // err = AddLink(dentry, vnode);
//   if (err)
//     goto out;

//   fs()->GetNodeManager().AllocNidDone(vnode->Ino());
//   // d_instantiate(dentry, inode);
//   UnlockNewInode(vnode);

//   fs()->GetSegmentManager().BalanceFs();

//   return 0;
// out:
//   vnode->ClearNlink();
//   UnlockNewInode(vnode);
//   fs()->GetNodeManager().AllocNidFailed(vnode->Ino());
//   return err;
// }
#endif

zx::result<bool> Dir::IsSubdir(Dir *possible_dir) {
  Dir *vn = possible_dir;
  fbl::RefPtr<VnodeF2fs> parent = nullptr;

  while (vn->Ino() != fs()->GetSuperblockInfo().GetRootIno()) {
    if (vn->Ino() == Ino()) {
      return zx::ok(true);
    }

    if (zx_status_t status = VnodeF2fs::Vget(fs(), vn->GetParentNid(), &parent); status != ZX_OK) {
      return zx::error(status);
    }

    vn = static_cast<Dir *>(parent.get());
  }
  return zx::ok(false);
}

zx_status_t Dir::Rename(fbl::RefPtr<fs::Vnode> _newdir, std::string_view oldname,
                        std::string_view newname, bool src_must_be_dir, bool dst_must_be_dir) {
  if (fs()->GetSuperblockInfo().TestCpFlags(CpFlag::kCpErrorFlag)) {
    return ZX_ERR_BAD_STATE;
  }

  fbl::RefPtr<Dir> new_dir = fbl::RefPtr<Dir>::Downcast(std::move(_newdir));
  bool is_same_dir = (new_dir.get() == this);
  {
    if (!fs::IsValidName(oldname) || !fs::IsValidName(newname)) {
      return ZX_ERR_INVALID_ARGS;
    }

    std::lock_guard dir_lock(dir_mutex_);

    timespec cur_time;
    clock_gettime(CLOCK_REALTIME, &cur_time);

    if (new_dir->GetNlink() == 0) {
      return ZX_ERR_NOT_FOUND;
    }

    fbl::RefPtr<Page> old_page;
    DirEntry *old_entry = FindEntry(oldname, &old_page);
    if (!old_entry) {
      return ZX_ERR_NOT_FOUND;
    }

    fbl::RefPtr<VnodeF2fs> old_vnode;
    nid_t old_ino = LeToCpu(old_entry->ino);
    if (zx_status_t err = VnodeF2fs::Vget(fs(), old_ino, &old_vnode); err != ZX_OK) {
      return err;
    }

    ZX_DEBUG_ASSERT(old_vnode->IsSameName(oldname));

    if (!old_vnode->IsDir() && (src_must_be_dir || dst_must_be_dir)) {
      return ZX_ERR_NOT_DIR;
    }

    ZX_DEBUG_ASSERT(!src_must_be_dir || old_vnode->IsDir());

    fbl::RefPtr<Page> old_dir_page;
    DirEntry *old_dir_entry = nullptr;
    if (old_vnode->IsDir()) {
      old_dir_entry = fbl::RefPtr<Dir>::Downcast(old_vnode)->ParentDir(&old_dir_page);
      if (!old_dir_entry) {
        return ZX_ERR_IO;
      }

      auto is_subdir = fbl::RefPtr<Dir>::Downcast(old_vnode)->IsSubdir(new_dir.get());
      if (is_subdir.is_error()) {
        return is_subdir.error_value();
      }
      if (*is_subdir) {
        return ZX_ERR_INVALID_ARGS;
      }
    }

    fs::SharedLock rlock(fs()->GetSuperblockInfo().GetFsLock(LockType::kFileOp));
    fbl::RefPtr<Page> new_page;
    DirEntry *new_entry = nullptr;
    if (is_same_dir) {
      new_entry = FindEntry(newname, &new_page);
    } else {
      new_entry = new_dir->FindEntrySafe(newname, &new_page);
    }

    if (new_entry) {
      ino_t new_ino = LeToCpu(new_entry->ino);
      fbl::RefPtr<VnodeF2fs> new_vnode;
      if (zx_status_t err = VnodeF2fs::Vget(fs(), new_ino, &new_vnode); err != ZX_OK) {
        return err;
      }

      ZX_DEBUG_ASSERT(new_vnode->IsSameName(newname));

      if (!new_vnode->IsDir() && (src_must_be_dir || dst_must_be_dir)) {
        return ZX_ERR_NOT_DIR;
      }

      if (old_vnode->IsDir() && !new_vnode->IsDir()) {
        return ZX_ERR_NOT_DIR;
      }

      if (!old_vnode->IsDir() && new_vnode->IsDir()) {
        return ZX_ERR_NOT_FILE;
      }

      if (is_same_dir && oldname == newname) {
        return ZX_OK;
      }

      if (old_dir_entry &&
          (!new_vnode->IsDir() || !fbl::RefPtr<Dir>::Downcast(new_vnode)->IsEmptyDir())) {
        return ZX_ERR_NOT_EMPTY;
      }

      old_vnode->SetName(newname);
      if (is_same_dir) {
        SetLink(new_entry, new_page, old_vnode.get());
      } else {
        new_dir->SetLinkSafe(new_entry, new_page, old_vnode.get());
      }

      new_vnode->SetCTime(cur_time);
      if (old_dir_entry) {
        new_vnode->DropNlink();
      }
      new_vnode->DropNlink();
      if (!new_vnode->GetNlink()) {
        fs()->AddOrphanInode(new_vnode.get());
      }
      new_vnode->WriteInode(false);
    } else {
      if (is_same_dir && oldname == newname) {
        return ZX_OK;
      }

      old_vnode->SetName(newname);

      if (is_same_dir) {
        if (zx_status_t err = AddLink(newname, old_vnode.get()); err != ZX_OK) {
          return err;
        }
        if (old_dir_entry) {
          IncNlink();
          WriteInode(false);
        }
      } else {
        if (zx_status_t err = new_dir->AddLinkSafe(newname, old_vnode.get()); err != ZX_OK) {
          return err;
        }
        if (old_dir_entry) {
          new_dir->IncNlink();
          new_dir->WriteInode(false);
        }
      }
    }

    old_vnode->SetParentNid(new_dir->Ino());
    old_vnode->SetCTime(cur_time);
    old_vnode->SetFlag(InodeInfoFlag::kNeedCp);
    old_vnode->MarkInodeDirty();

    DeleteEntry(old_entry, old_page, nullptr);

    if (old_dir_entry) {
      if (!is_same_dir) {
        fbl::RefPtr<Dir>::Downcast(old_vnode)->SetLinkSafe(old_dir_entry, old_dir_page,
                                                           new_dir.get());
      }
      DropNlink();
      WriteInode(false);
    }

    // Add new parent directory to VnodeSet to ensure consistency of renamed vnode.
    fs()->GetSuperblockInfo().AddVnodeToVnodeSet(InoType::kModifiedDirIno, new_dir->Ino());
    if (old_vnode->IsDir()) {
      fs()->GetSuperblockInfo().AddVnodeToVnodeSet(InoType::kModifiedDirIno, old_vnode->Ino());
    }
  }

  fs()->GetSegmentManager().BalanceFs();
  return ZX_OK;
}

zx_status_t Dir::Create(std::string_view name, uint32_t mode, fbl::RefPtr<fs::Vnode> *out) {
  if (fs()->GetSuperblockInfo().TestCpFlags(CpFlag::kCpErrorFlag)) {
    return ZX_ERR_BAD_STATE;
  }

  if (!fs::IsValidName(name)) {
    return ZX_ERR_INVALID_ARGS;
  }
  zx_status_t ret = ZX_OK;
  {
    std::lock_guard dir_lock(dir_mutex_);
    if (GetNlink() == 0)
      return ZX_ERR_NOT_FOUND;

    if (auto ret = FindEntry(name); !ret.is_error()) {
      return ZX_ERR_ALREADY_EXISTS;
    }

    if (S_ISDIR(mode)) {
      ret = Mkdir(name, mode, out);
    } else {
      ret = DoCreate(name, mode, out);
    }
  }
  if (ret == ZX_OK) {
    fs()->GetSegmentManager().BalanceFs();
    ret = (*out)->OpenValidating(fs::VnodeConnectionOptions(), nullptr);
  }
  return ret;
}

zx_status_t Dir::Unlink(std::string_view name, bool must_be_dir) {
  if (fs()->GetSuperblockInfo().TestCpFlags(CpFlag::kCpErrorFlag)) {
    return ZX_ERR_BAD_STATE;
  }

  zx_status_t ret = ZX_OK;
  {
    std::lock_guard dir_lock(dir_mutex_);
    fbl::RefPtr<fs::Vnode> vn;
    if (zx_status_t status = DoLookup(name, &vn); status != ZX_OK) {
      return status;
    }

    VnodeF2fs *vnode = (VnodeF2fs *)vn.get();
    if (vnode->IsDir()) {
      ret = Rmdir(static_cast<Dir *>(vnode), name);
    } else {
      if (must_be_dir) {
        return ZX_ERR_NOT_DIR;
      }
      ret = DoUnlink(vnode, name);
    }
  }
  if (ret == ZX_OK) {
    fs()->GetSegmentManager().BalanceFs();
  }
  return ret;
}

}  // namespace f2fs
