// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string.h>
#include <sys/stat.h>

#include "src/storage/f2fs/f2fs.h"

namespace f2fs {

zx_status_t Dir::NewInode(uint32_t mode, fbl::RefPtr<VnodeF2fs> *out) {
  SbInfo &sbi = Vfs()->GetSbInfo();
  nid_t ino;
  fbl::RefPtr<VnodeF2fs> vnode_refptr;
  VnodeF2fs *vnode = nullptr;

  do {
    fs::SharedLock rlock(sbi.fs_lock[static_cast<int>(LockType::kFileOp)]);
    if (!Vfs()->GetNodeManager().AllocNid(&ino)) {
      Iput(vnode);
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
  vnode->SetGeneration(sbi.s_next_generation++);

  if (TestOpt(&sbi, kMountInlineDentry) && vnode->IsDir())
    vnode->SetFlag(InodeInfoFlag::kInlineDentry);

  vnode->SetFlag(InodeInfoFlag::kNewInode);
  Vfs()->InsertVnode(vnode);
  vnode->MarkInodeDirty();

  *out = std::move(vnode_refptr);
  return ZX_OK;
}

int Dir::IsMultimediaFile(VnodeF2fs *vnode, const char *sub) {
  int slen = vnode->GetNameLen();
  int sublen = static_cast<int>(strlen(sub));
  int ret;

  if (sublen > slen)
    return 1;

  if (ret = memcmp(vnode->GetName() + slen - sublen, sub, sublen);
      ret != 0) { /* compare upper case */
    int i;
    char upper_sub[8];
    for (i = 0; i < sublen && i < static_cast<int>(sizeof(upper_sub)); i++)
      upper_sub[i] = static_cast<char>(toupper(sub[i]));
    return memcmp(vnode->GetName() + slen - sublen, upper_sub, sublen);
  }

  return ret;
}

/**
 * Set multimedia files as cold files for hot/cold data separation
 */
inline void Dir::SetColdFile(VnodeF2fs *vnode) {
  int i;
  uint8_t(*extlist)[8] = Vfs()->RawSb().extension_list;

  int count = LeToCpu(Vfs()->RawSb().extension_count);
  for (i = 0; i < count; i++) {
    if (!IsMultimediaFile(vnode, reinterpret_cast<const char *>(extlist[i]))) {
      vnode->SetAdvise(FAdvise::kCold);
      break;
    }
  }
}

zx_status_t Dir::DoCreate(std::string_view name, uint32_t mode, fbl::RefPtr<fs::Vnode> *out) {
  SbInfo &sbi = Vfs()->GetSbInfo();
  fbl::RefPtr<VnodeF2fs> vnode_refptr;
  VnodeF2fs *vnode = nullptr;

  if (zx_status_t err = NewInode(S_IFREG | mode, &vnode_refptr); err != ZX_OK)
    return err;
  vnode = vnode_refptr.get();

  vnode->SetName(name);

  if (!TestOpt(&sbi, kMountDisableExtIdentify))
    SetColdFile(vnode);

  vnode->SetFlag(InodeInfoFlag::kIncLink);
  {
    fs::SharedLock rlock(sbi.fs_lock[static_cast<int>(LockType::kFileOp)]);
    if (zx_status_t err = AddLink(name, vnode); err != ZX_OK) {
      vnode->ClearNlink();
      vnode->UnlockNewInode();
      Iput(vnode);
      Vfs()->GetNodeManager().AllocNidFailed(vnode->Ino());
      return err;
    }
  }

  Vfs()->GetNodeManager().AllocNidDone(vnode->Ino());

#if 0  // porting needed
  // if (!sbi.por_doing)
  //   d_instantiate(dentry, inode);
#endif
  vnode->UnlockNewInode();

  Vfs()->Segmgr().BalanceFs();

  *out = std::move(vnode_refptr);
  return ZX_OK;
}

zx_status_t Dir::Link(std::string_view name, fbl::RefPtr<fs::Vnode> _target) {
  VnodeF2fs *target = static_cast<VnodeF2fs *>(_target.get());

  ZX_DEBUG_ASSERT(fs::IsValidName(name));

  if (target->IsDir())
    return ZX_ERR_NOT_FILE;

  Page *old_entry_page = nullptr;
  DirEntry *old_entry = FindEntry(name, &old_entry_page);
  if (old_entry != nullptr) {
    nid_t old_ino = LeToCpu(old_entry->ino);
    F2fsPutPage(old_entry_page, 0);
    if (old_ino == target->Ino())
      return ZX_ERR_ALREADY_EXISTS;
  }

  timespec cur_time;
  clock_gettime(CLOCK_REALTIME, &cur_time);
  target->SetCTime(cur_time);

#if 0  // porting needed
  // AtomicInc(&inode->i_count);
#endif
  {
    SbInfo &sbi = Vfs()->GetSbInfo();
    fs::SharedLock rlock(sbi.fs_lock[static_cast<int>(LockType::kFileOp)]);
    target->SetFlag(InodeInfoFlag::kIncLink);
    if (zx_status_t err = AddLink(name, target); err != ZX_OK) {
      target->ClearFlag(InodeInfoFlag::kIncLink);
      Iput(target);
      return err;
    }
  }

#if 0  // porting needed
  // d_instantiate(dentry, inode);
#endif

  Vfs()->Segmgr().BalanceFs();

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
  DirEntry *de;
  Page *page = nullptr;

  if (!fs::IsValidName(name)) {
    return ZX_ERR_INVALID_ARGS;
  }

  de = FindEntry(name, &page);
  if (de) {
    nid_t ino = LeToCpu(de->ino);
#if 0  // porting needed
    // if (!f2fs_has_inline_dentry(dir))
    //   kunmap(page);
#endif
    F2fsPutPage(page, 0);

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
  Page *page = nullptr;

  de = FindEntry(name, &page);
  if (de == nullptr) {
    return ZX_ERR_NOT_FOUND;
  }

  {
    SbInfo &sbi = Vfs()->GetSbInfo();
    fs::SharedLock rlock(sbi.fs_lock[static_cast<int>(LockType::kFileOp)]);
    if (zx_status_t err = Vfs()->CheckOrphanSpace(); err != ZX_OK) {
#if 0  // porting needed
    // if (!f2fs_has_inline_dentry(dir))
    //   kunmap(page);
#endif
      F2fsPutPage(page, 0);
      return err;
    }

    DeleteEntry(de, page, vnode);
  }

  Vfs()->Segmgr().BalanceFs();
  F2fsPutPage(page, 0);
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

//   //   Vfs()->Segmgr().BalanceFs();

//   //   return err;
//   // out:
//   //   vnode->ClearNlink();
//   //   UnlockNewInode(vnode);
//   //   // Iput(inode);
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

#if 0  // porting needed
  // mapping_set_gfp_mask(inode->i_mapping, GFP_NOFS | __GFP_ZERO);
#endif

  vnode->SetFlag(InodeInfoFlag::kIncLink);
  {
    SbInfo &sbi = Vfs()->GetSbInfo();
    fs::SharedLock rlock(sbi.fs_lock[static_cast<int>(LockType::kFileOp)]);
    if (zx_status_t err = AddLink(name, vnode); err != ZX_OK) {
      vnode->ClearFlag(InodeInfoFlag::kIncLink);
      vnode->ClearNlink();
      vnode->UnlockNewInode();
      Iput(vnode);
      Vfs()->GetNodeManager().AllocNidFailed(vnode->Ino());
      return err;
    }
  }
  Vfs()->GetNodeManager().AllocNidDone(vnode->Ino());

#if 0  // porting needed
  // d_instantiate(dentry, inode);
#endif
  vnode->UnlockNewInode();

  Vfs()->Segmgr().BalanceFs();

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

//   Vfs()->Segmgr().BalanceFs();

//   return 0;
// out:
//   vnode->ClearNlink();
//   UnlockNewInode(vnode);
//   Iput(vnode);
//   Vfs()->GetNodeManager().AllocNidFailed(vnode->Ino());
//   return err;
// }
#endif

zx::status<bool> Dir::IsSubdir(Dir *possible_dir) {
  Dir *vn = possible_dir;
  fbl::RefPtr<VnodeF2fs> parent = nullptr;

  while (vn->Ino() != RootIno(&Vfs()->GetSbInfo())) {
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
  SbInfo &sbi = Vfs()->GetSbInfo();
  fbl::RefPtr<VnodeF2fs> old_vn_ref;
  fbl::RefPtr<VnodeF2fs> new_vn_ref;
  Dir *old_dir = this;
  Dir *new_dir = static_cast<Dir *>(_newdir.get());
  VnodeF2fs *old_vnode = nullptr;
  nid_t old_ino;
  VnodeF2fs *new_vnode = nullptr;
  nid_t new_ino;
  Page *old_dir_page = nullptr;
  Page *old_page = nullptr;
  Page *new_page = nullptr;
  DirEntry *old_dir_entry = nullptr;
  DirEntry *old_entry;
  DirEntry *new_entry;
  timespec cur_time;

  auto reset_pages = [&] {
    F2fsPutPage(old_dir_page, 0);
    F2fsPutPage(new_page, 0);
    F2fsPutPage(old_page, 0);
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
    fs::SharedLock rlock(sbi.fs_lock[static_cast<int>(LockType::kFileOp)]);

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
      new_dir->SetLink(new_entry, new_page, old_vnode);

      new_vnode->SetCTime(cur_time);
      if (old_dir_entry)
        new_vnode->DropNlink();
      new_vnode->DropNlink();
      if (!new_vnode->GetNlink())
        Vfs()->AddOrphanInode(new_vnode);
      new_vnode->WriteInode(nullptr);
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
        new_dir->WriteInode(nullptr);
      }
    }

    old_vnode->SetCTime(cur_time);
    old_vnode->SetFlag(InodeInfoFlag::kNeedCp);
    old_vnode->MarkInodeDirty();

    // TODO(djkim): remove this after pager is available
    // If old_dir == new_dir, old_page is not up-to-date after add new entry with newname
    // Therefore, old_page should be read again, unless pager is implemented
    if (old_dir == new_dir) {
      F2fsPutPage(old_page, 0);
      old_entry = FindEntry(oldname, &old_page);
    }

    DeleteEntry(old_entry, old_page, nullptr);

    if (old_dir_entry) {
      if (old_dir != new_dir) {
        (static_cast<Dir *>(old_vnode))->SetLink(old_dir_entry, old_dir_page, new_dir);
      } else {
#if 0  // porting needed
       // if (!f2fs_has_inline_dentry(old_inode))
       //   kunmap(old_dir_page);
#endif
        F2fsPutPage(old_dir_page, 0);
      }
      old_dir->DropNlink();
      old_dir->WriteInode(nullptr);
    }
  } while (false);

  Vfs()->Segmgr().BalanceFs();
  reset_pages();
  return ZX_OK;
}

zx_status_t Dir::Create(std::string_view name, uint32_t mode, fbl::RefPtr<fs::Vnode> *out) {
  Page *page = nullptr;
  zx_status_t status = ZX_OK;

  if (!fs::IsValidName(name)) {
    return ZX_ERR_INVALID_ARGS;
  }

  if (GetNlink() == 0)
    return ZX_ERR_NOT_FOUND;

  if (FindEntry(name, &page) != nullptr) {
    F2fsPutPage(page, 0);
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
