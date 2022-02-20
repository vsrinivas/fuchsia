// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string.h>
#include <sys/stat.h>

#include "src/storage/f2fs/f2fs.h"

#ifndef __Fuchsia__
#include "lib/stdcompat/string_view.h"
#endif  // __Fuchsia__

namespace f2fs {

zx_status_t Dir::NewInode(uint32_t mode, fbl::RefPtr<VnodeF2fs> *out) {
  SuperblockInfo &superblock_info = Vfs()->GetSuperblockInfo();
  nid_t ino;
  fbl::RefPtr<VnodeF2fs> vnode_refptr;
  VnodeF2fs *vnode = nullptr;

  do {
    fs::SharedLock rlock(superblock_info.GetFsLock(LockType::kFileOp));
    if (!Vfs()->GetNodeManager().AllocNid(ino)) {
      return ZX_ERR_NO_SPACE;
    }
  } while (false);

  VnodeF2fs::Allocate(Vfs(), ino, mode, &vnode_refptr);

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
  vnode->ClearNlink();
  vnode->InitBlocks();

  timespec cur_time;
  clock_gettime(CLOCK_REALTIME, &cur_time);
  vnode->SetATime(cur_time);
  vnode->SetCTime(cur_time);
  vnode->SetMTime(cur_time);
  vnode->SetGeneration(superblock_info.GetNextGeneration());
  superblock_info.IncNextGeneration();

  if (superblock_info.TestOpt(kMountInlineDentry) && vnode->IsDir())
    vnode->SetFlag(InodeInfoFlag::kInlineDentry);

  vnode->SetFlag(InodeInfoFlag::kNewInode);
  Vfs()->InsertVnode(vnode);
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
  const std::vector<std::string> &extension_list = Vfs()->GetSuperblockInfo().GetExtensionList();

  for (const auto &extension : extension_list) {
    if (IsMultimediaFile(vnode, extension)) {
      vnode.SetAdvise(FAdvise::kCold);
      break;
    }
  }
}

zx_status_t Dir::DoCreate(std::string_view name, uint32_t mode, fbl::RefPtr<fs::Vnode> *out) {
  SuperblockInfo &superblock_info = Vfs()->GetSuperblockInfo();
  fbl::RefPtr<VnodeF2fs> vnode_refptr;
  VnodeF2fs *vnode = nullptr;

  if (zx_status_t err = NewInode(S_IFREG | mode, &vnode_refptr); err != ZX_OK)
    return err;
  vnode = vnode_refptr.get();

  vnode->SetName(name);

  if (!superblock_info.TestOpt(kMountDisableExtIdentify))
    SetColdFile(*vnode);

  vnode->SetFlag(InodeInfoFlag::kIncLink);
  {
    fs::SharedLock rlock(superblock_info.GetFsLock(LockType::kFileOp));
    if (zx_status_t err = AddLink(name, vnode); err != ZX_OK) {
      vnode->ClearNlink();
      vnode->UnlockNewInode();
      Vfs()->GetNodeManager().AllocNidFailed(vnode->Ino());
      return err;
    }
  }

  Vfs()->GetNodeManager().AllocNidDone(vnode->Ino());

#if 0  // porting needed
  // if (!superblock_info.IsOnRecovery())
  //   d_instantiate(dentry, inode);
#endif
  vnode->UnlockNewInode();

  Vfs()->GetSegmentManager().BalanceFs();

  *out = std::move(vnode_refptr);
  return ZX_OK;
}

zx_status_t Dir::Link(std::string_view name, fbl::RefPtr<fs::Vnode> new_child) {
  VnodeF2fs *target = static_cast<VnodeF2fs *>(new_child.get());

  ZX_DEBUG_ASSERT(fs::IsValidName(name));

  if (target->IsDir())
    return ZX_ERR_NOT_FILE;

  if (auto old_entry = FindEntry(name); !old_entry.is_error()) {
    return ZX_ERR_ALREADY_EXISTS;
  }

  timespec cur_time;
  clock_gettime(CLOCK_REALTIME, &cur_time);
  target->SetCTime(cur_time);

  {
    fs::SharedLock rlock(Vfs()->GetSuperblockInfo().GetFsLock(LockType::kFileOp));
    target->SetFlag(InodeInfoFlag::kIncLink);
    if (zx_status_t err = AddLink(name, target); err != ZX_OK) {
      target->ClearFlag(InodeInfoFlag::kIncLink);
      return err;
    }
  }

#if 0  // porting needed
  // d_instantiate(dentry, inode);
#endif

  Vfs()->GetSegmentManager().BalanceFs();

  return ZX_OK;
}

#if 0  // porting needed
// dentry *Dir::F2fsGetParent(dentry *child) {
//   return nullptr;
//   // qstr dotdot = QSTR_INIT("..", 2);
//   // uint64_t ino = Inode_by_name(child->d_inode, &dotdot);
//   // if (!ino)
//   //   return ErrPtr(-ENOENT);
//   // return d_obtain_alias(f2fs_iget(child->d_inode->i_sb, ino));
// }
#endif

zx_status_t Dir::DoLookup(std::string_view name, fbl::RefPtr<fs::Vnode> *out) {
  fbl::RefPtr<VnodeF2fs> vn;

  if (!fs::IsValidName(name)) {
    return ZX_ERR_INVALID_ARGS;
  }

  if (auto dir_entry = FindEntry(name); !dir_entry.is_error()) {
    nid_t ino = LeToCpu((*dir_entry).ino);
#if 0  // porting needed
    // if (!f2fs_has_inline_dentry(dir))
    //   kunmap(page);
#endif

    if (zx_status_t ret = VnodeF2fs::Vget(Vfs(), ino, &vn); ret != ZX_OK)
      return ret;

    *out = std::move(vn);

    return ZX_OK;
  }

  return ZX_ERR_NOT_FOUND;
}

zx_status_t Dir::Lookup(std::string_view name, fbl::RefPtr<fs::Vnode> *out) {
  return DoLookup(name, out);
}

zx_status_t Dir::DoUnlink(VnodeF2fs *vnode, std::string_view name) {
  DirEntry *de;
  fbl::RefPtr<Page> page;

  de = FindEntry(name, &page);
  if (de == nullptr) {
    return ZX_ERR_NOT_FOUND;
  }

  {
    fs::SharedLock rlock(Vfs()->GetSuperblockInfo().GetFsLock(LockType::kFileOp));
    if (zx_status_t err = Vfs()->CheckOrphanSpace(); err != ZX_OK) {
#if 0  // porting needed
    // if (!f2fs_has_inline_dentry(dir))
    //   kunmap(page);
#endif
      Page::PutPage(std::move(page), false);
      return err;
    }

    DeleteEntry(de, page.get(), vnode);
  }

  Page::PutPage(std::move(page), false);
  Vfs()->GetSegmentManager().BalanceFs();
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
//   //   Vfs()->GetNodeManager().AllocNidDone(vnode->Ino());

//   //   // d_instantiate(dentry, vnode);
//   //   UnlockNewInode(vnode);

//   //   Vfs()->GetSegmentManager().BalanceFs();

//   //   return err;
//   // out:
//   //   vnode->ClearNlink();
//   //   UnlockNewInode(vnode);
//   //   Vfs()->GetNodeManager().AllocNidFailed(vnode->Ino());
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
    SuperblockInfo &superblock_info = Vfs()->GetSuperblockInfo();
    fs::SharedLock rlock(superblock_info.GetFsLock(LockType::kFileOp));
    if (zx_status_t err = AddLink(name, vnode); err != ZX_OK) {
      vnode->ClearFlag(InodeInfoFlag::kIncLink);
      vnode->ClearNlink();
      vnode->UnlockNewInode();
      Vfs()->GetNodeManager().AllocNidFailed(vnode->Ino());
      return err;
    }
  }
  Vfs()->GetNodeManager().AllocNidDone(vnode->Ino());

#if 0  // porting needed
  // d_instantiate(dentry, inode);
#endif
  vnode->UnlockNewInode();

  Vfs()->GetSegmentManager().BalanceFs();

  *out = std::move(vnode_refptr);
  return ZX_OK;
}

zx_status_t Dir::Rmdir(Dir *vnode, std::string_view name) {
  if (vnode->IsEmptyDir())
    return DoUnlink(vnode, name);
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

//   Vfs()->GetNodeManager().AllocNidDone(vnode->Ino());
//   // d_instantiate(dentry, inode);
//   UnlockNewInode(vnode);

//   Vfs()->GetSegmentManager().BalanceFs();

//   return 0;
// out:
//   vnode->ClearNlink();
//   UnlockNewInode(vnode);
//   Vfs()->GetNodeManager().AllocNidFailed(vnode->Ino());
//   return err;
// }
#endif

zx::status<bool> Dir::IsSubdir(Dir *possible_dir) {
  Dir *vn = possible_dir;
  fbl::RefPtr<VnodeF2fs> parent = nullptr;

  while (vn->Ino() != Vfs()->GetSuperblockInfo().GetRootIno()) {
    if (vn->Ino() == Ino()) {
      return zx::ok(true);
    }

    if (zx_status_t status = VnodeF2fs::Vget(Vfs(), vn->GetParentNid(), &parent); status != ZX_OK) {
      return zx::error(status);
    }

    vn = static_cast<Dir *>(parent.get());
  }
  return zx::ok(false);
}

zx_status_t Dir::Rename(fbl::RefPtr<fs::Vnode> _newdir, std::string_view oldname,
                        std::string_view newname, bool src_must_be_dir, bool dst_must_be_dir) {
  fbl::RefPtr<VnodeF2fs> old_vn_ref;
  fbl::RefPtr<VnodeF2fs> new_vn_ref;
  Dir *old_dir = this;
  Dir *new_dir = static_cast<Dir *>(_newdir.get());
  VnodeF2fs *old_vnode = nullptr;
  nid_t old_ino;
  VnodeF2fs *new_vnode = nullptr;
  nid_t new_ino;
  fbl::RefPtr<Page> old_dir_page;
  fbl::RefPtr<Page> old_page;
  fbl::RefPtr<Page> new_page;
  DirEntry *old_dir_entry = nullptr;
  DirEntry *old_entry;
  DirEntry *new_entry;
  timespec cur_time;

  auto reset_pages = [&] {
    if (old_dir_page) {
      Page::PutPage(std::move(old_dir_page), false);
    }
    if (new_page) {
      Page::PutPage(std::move(new_page), false);
    }
    if (old_page) {
      Page::PutPage(std::move(old_page), false);
    }
  };

  ZX_DEBUG_ASSERT(fs::IsValidName(oldname));
  ZX_DEBUG_ASSERT(fs::IsValidName(newname));

  clock_gettime(CLOCK_REALTIME, &cur_time);

  if (new_dir->GetNlink() == 0)
    return ZX_ERR_NOT_FOUND;

  old_entry = FindEntry(oldname, &old_page);
  if (!old_entry) {
    return ZX_ERR_NOT_FOUND;
  }

  old_ino = LeToCpu(old_entry->ino);
  if (zx_status_t err = VnodeF2fs::Vget(Vfs(), old_ino, &old_vn_ref); err != ZX_OK) {
    reset_pages();
    return err;
  }

  old_vnode = old_vn_ref.get();
  ZX_ASSERT(old_vnode->IsSameName(oldname));

  if (!old_vnode->IsDir() && (src_must_be_dir || dst_must_be_dir)) {
    reset_pages();
    return ZX_ERR_NOT_DIR;
  }

  ZX_ASSERT(!src_must_be_dir || old_vnode->IsDir());

  if (old_vnode->IsDir()) {
    old_dir_entry = (static_cast<Dir *>(old_vnode))->ParentDir(&old_dir_page);
    if (!old_dir_entry) {
#if 0  // porting needed
      // if (!f2fs_has_inline_dentry(old_dir))
      //   kunmap(old_page);
#endif
      reset_pages();
      return ZX_ERR_IO;
    }

    auto is_subdir = (static_cast<Dir *>(old_vnode))->IsSubdir(new_dir);
    if (is_subdir.is_error()) {
      reset_pages();
      return is_subdir.error_value();
    }
    if (*is_subdir) {
      reset_pages();
      return ZX_ERR_INVALID_ARGS;
    }
  }

  do {
    fs::SharedLock rlock(Vfs()->GetSuperblockInfo().GetFsLock(LockType::kFileOp));

    new_entry = new_dir->FindEntry(newname, &new_page);
    if (new_entry) {
      new_ino = LeToCpu(new_entry->ino);
      if (zx_status_t err = VnodeF2fs::Vget(Vfs(), new_ino, &new_vn_ref); err != ZX_OK) {
        if (old_dir_entry) {
#if 0  // porting needed
       // if (!f2fs_has_inline_dentry(old_inode))
       //   kunmap(old_dir_page);
#endif
        }

#if 0  // porting needed
      // if (!f2fs_has_inline_dentry(old_dir))
      //   kunmap(old_page);
#endif
        reset_pages();
        return err;
      }

      new_vnode = new_vn_ref.get();
      ZX_ASSERT(new_vnode->IsSameName(newname));

      if (!new_vnode->IsDir() && (src_must_be_dir || dst_must_be_dir)) {
        reset_pages();
        return ZX_ERR_NOT_DIR;
      }

      if (old_vnode->IsDir() && !new_vnode->IsDir()) {
        reset_pages();
        return ZX_ERR_NOT_DIR;
      }

      if (!old_vnode->IsDir() && new_vnode->IsDir()) {
        reset_pages();
        return ZX_ERR_NOT_FILE;
      }

      if (old_dir == new_dir && oldname == newname) {
        reset_pages();
        return ZX_OK;
      }

      if (old_dir_entry &&
          (!new_vnode->IsDir() || !(static_cast<Dir *>(new_vnode))->IsEmptyDir())) {
#if 0  // porting needed
       // if (!f2fs_has_inline_dentry(old_inode))
       //   kunmap(old_dir_page);
      // if (!f2fs_has_inline_dentry(old_dir))
      //   kunmap(old_page);
#endif
        reset_pages();
        return ZX_ERR_NOT_EMPTY;
      }

      old_vnode->SetName(newname);
      new_dir->SetLink(new_entry, new_page.get(), old_vnode);

      new_vnode->SetCTime(cur_time);
      if (old_dir_entry)
        new_vnode->DropNlink();
      new_vnode->DropNlink();
      if (!new_vnode->GetNlink())
        Vfs()->AddOrphanInode(new_vnode);
      new_vnode->WriteInode(false);
    } else {
      if (old_dir == new_dir && oldname == newname) {
        reset_pages();
        return ZX_OK;
      }

      old_vnode->SetName(newname);
      if (zx_status_t err = new_dir->AddLink(newname, old_vnode); err != ZX_OK) {
        if (old_dir_entry) {
#if 0  // porting needed
       // if (!f2fs_has_inline_dentry(old_inode))
       //   kunmap(old_dir_page);
#endif
        }

#if 0  // porting needed
      // if (!f2fs_has_inline_dentry(old_dir))
      //   kunmap(old_page);
#endif
        reset_pages();
        return err;
      }

      if (old_dir_entry) {
        new_dir->IncNlink();
        new_dir->WriteInode(false);
      }
    }

    old_vnode->SetParentNid(new_dir->Ino());
    old_vnode->SetCTime(cur_time);
    old_vnode->SetFlag(InodeInfoFlag::kNeedCp);
    old_vnode->MarkInodeDirty();

    DeleteEntry(old_entry, old_page.get(), nullptr);

    if (old_dir_entry) {
      if (old_dir != new_dir) {
        (static_cast<Dir *>(old_vnode))->SetLink(old_dir_entry, old_dir_page.get(), new_dir);
      } else {
#if 0  // porting needed
       // if (!f2fs_has_inline_dentry(old_inode))
       //   kunmap(old_dir_page);
#endif
        Page::PutPage(std::move(old_dir_page), false);
      }
      old_dir->DropNlink();
      old_dir->WriteInode(false);
    }
  } while (false);

  reset_pages();
  Vfs()->GetSegmentManager().BalanceFs();
  return ZX_OK;
}

zx_status_t Dir::Create(std::string_view name, uint32_t mode, fbl::RefPtr<fs::Vnode> *out) {
  zx_status_t status = ZX_OK;

  if (!fs::IsValidName(name)) {
    return ZX_ERR_INVALID_ARGS;
  }

  if (GetNlink() == 0)
    return ZX_ERR_NOT_FOUND;

  if (auto ret = FindEntry(name); !ret.is_error()) {
    return ZX_ERR_ALREADY_EXISTS;
  }

  if (S_ISDIR(mode)) {
    status = Mkdir(name, mode, out);
  } else {
    status = DoCreate(name, mode, out);
  }

  if (status != ZX_OK)
    return status;

  status = (*out)->OpenValidating(fs::VnodeConnectionOptions(), nullptr);
  return status;
}

zx_status_t Dir::Unlink(std::string_view name, bool must_be_dir) {
  fbl::RefPtr<fs::Vnode> vn;

  if (zx_status_t status = DoLookup(name, &vn); status != ZX_OK) {
    return status;
  }

  VnodeF2fs *vnode = (VnodeF2fs *)vn.get();

  if (vnode->IsDir())
    return Rmdir(static_cast<Dir *>(vnode), name);

  if (must_be_dir)
    return ZX_ERR_NOT_DIR;

  return DoUnlink(vnode, name);
}

}  // namespace f2fs
