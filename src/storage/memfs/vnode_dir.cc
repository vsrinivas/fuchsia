// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/memfs/vnode_dir.h"

#include <sys/stat.h>

#include "src/storage/memfs/dnode.h"
#include "src/storage/memfs/vnode_file.h"
#include "src/storage/memfs/vnode_vmo.h"

namespace memfs {

VnodeDir::VnodeDir(PlatformVfs* vfs, uint64_t max_file_size)
    : Vnode(vfs), max_file_size_(max_file_size) {
  link_count_ = 1;  // Implied '.'
}

VnodeDir::~VnodeDir() = default;

fs::VnodeProtocolSet VnodeDir::GetProtocols() const { return fs::VnodeProtocol::kDirectory; }

void VnodeDir::Notify(std::string_view name, fuchsia_io::wire::WatchEvent event) {
  watcher_.Notify(name, event);
}

zx_status_t VnodeDir::WatchDir(fs::Vfs* vfs, fuchsia_io::wire::WatchMask mask, uint32_t options,
                               fidl::ServerEnd<fuchsia_io::DirectoryWatcher> watcher) {
  return watcher_.WatchDir(vfs, this, mask, options, std::move(watcher));
}

zx_status_t VnodeDir::GetVmo(fuchsia_io::wire::VmoFlags flags, zx::vmo* out_vmo) {
  return ZX_ERR_ACCESS_DENIED;
}

zx_status_t VnodeDir::Lookup(std::string_view name, fbl::RefPtr<fs::Vnode>* out) {
  if (!IsDirectory()) {
    return ZX_ERR_NOT_FOUND;
  }
  Dnode* dn;
  zx_status_t r = dnode_->Lookup(name, &dn);
  ZX_DEBUG_ASSERT(r <= 0);
  if (r == ZX_OK) {
    if (dn == nullptr) {
      // Looking up our own vnode
      *out = fbl::RefPtr<VnodeDir>(this);
    } else {
      // Looking up a different vnode
      *out = dn->AcquireVnode();
    }
  }
  return r;
}

zx_status_t VnodeDir::GetAttributes(fs::VnodeAttributes* attr) {
  *attr = fs::VnodeAttributes();
  attr->inode = ino_;
  attr->mode = V_TYPE_DIR | V_IRUSR;
  attr->content_size = 0;
  attr->storage_size = 0;
  attr->link_count = link_count_;
  attr->creation_time = create_time_;
  attr->modification_time = modify_time_;
  return ZX_OK;
}

zx_status_t VnodeDir::GetNodeInfoForProtocol([[maybe_unused]] fs::VnodeProtocol protocol,
                                             [[maybe_unused]] fs::Rights rights,
                                             fs::VnodeRepresentation* info) {
  *info = fs::VnodeRepresentation::Directory();
  return ZX_OK;
}

zx_status_t VnodeDir::Readdir(fs::VdirCookie* cookie, void* data, size_t len, size_t* out_actual) {
  fs::DirentFiller df(data, len);
  if (!IsDirectory()) {
    // This WAS a directory, but it has been deleted.
    *out_actual = 0;
    return ZX_OK;
  }
  dnode_->Readdir(&df, cookie);
  *out_actual = df.BytesFilled();
  return ZX_OK;
}

// postcondition: reference taken on vn returned through "out"
zx_status_t VnodeDir::Create(std::string_view name, uint32_t mode, fbl::RefPtr<fs::Vnode>* out) {
  zx_status_t status;
  if ((status = CanCreate(name)) != ZX_OK) {
    return status;
  }

  fbl::AllocChecker ac;
  fbl::RefPtr<memfs::Vnode> vn;
  {
    std::lock_guard lock(mutex_);
    if (S_ISDIR(mode)) {
      vn = fbl::AdoptRef(new (&ac) memfs::VnodeDir(vfs(), max_file_size_));
    } else {
      vn = fbl::AdoptRef(new (&ac) memfs::VnodeFile(vfs(), max_file_size_));
    }
  }

  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }

  if ((status = AttachVnode(vn, name, S_ISDIR(mode))) != ZX_OK) {
    return status;
  }
  *out = std::move(vn);

#ifdef __Fuchsia__
  CheckInotifyFilterAndNotify(fuchsia_io::wire::InotifyWatchMask::kCreate);
#endif
  return ZX_OK;
}

zx_status_t VnodeDir::Unlink(std::string_view name, bool must_be_dir) {
  if (!IsDirectory()) {
    // Calling unlink from unlinked, empty directory
    return ZX_ERR_BAD_STATE;
  }
  Dnode* dn;
  zx_status_t r;
  if ((r = dnode_->Lookup(name, &dn)) != ZX_OK) {
    return r;
  } else if (dn == nullptr) {
    // Cannot unlink directory 'foo' using the argument 'foo/.'
    return ZX_ERR_UNAVAILABLE;
  } else if (!dn->IsDirectory() && must_be_dir) {
    // Path ending in "/" was requested, implying that the dnode must be a directory
    return ZX_ERR_NOT_DIR;
  } else if ((r = dn->CanUnlink()) != ZX_OK) {
    return r;
  }

  dn->Detach();
  return ZX_OK;
}

zx_status_t VnodeDir::Rename(fbl::RefPtr<fs::Vnode> _newdir, std::string_view oldname,
                             std::string_view newname, bool src_must_be_dir, bool dst_must_be_dir) {
  auto newdir = fbl::RefPtr<Vnode>::Downcast(std::move(_newdir));

  if (!IsDirectory() || !newdir->IsDirectory()) {
    // Not linked into the directory hierachy.
    return ZX_ERR_NOT_FOUND;
  }

  Dnode* olddn;
  zx_status_t r;
  // The source must exist
  if ((r = dnode_->Lookup(oldname, &olddn)) != ZX_OK) {
    return r;
  }
  ZX_DEBUG_ASSERT(olddn != nullptr);

  if (!olddn->IsDirectory() && (src_must_be_dir || dst_must_be_dir)) {
    return ZX_ERR_NOT_DIR;
  } else if ((newdir->ino() == ino_) && (oldname == newname)) {
    // Renaming a file or directory to itself?
    // Shortcut success case
    return ZX_OK;
  }

  // Verify that the destination is not a subdirectory of the source (if
  // both are directories).
  if (olddn->IsSubdirectory(newdir->dnode_)) {
    return ZX_ERR_INVALID_ARGS;
  }

  // The destination may or may not exist
  Dnode* targetdn;
  r = newdir->dnode_->Lookup(newname, &targetdn);
  bool target_exists = (r == ZX_OK);
  if (target_exists) {
    ZX_DEBUG_ASSERT(targetdn != nullptr);
    // The target exists. Validate and unlink it.
    if (olddn == targetdn) {
      // Cannot rename node to itself
      return ZX_ERR_INVALID_ARGS;
    }
    if (olddn->IsDirectory() != targetdn->IsDirectory()) {
      // Cannot rename files to directories (and vice versa)
      return olddn->IsDirectory() ? ZX_ERR_NOT_DIR : ZX_ERR_NOT_FILE;
    } else if ((r = targetdn->CanUnlink()) != ZX_OK) {
      return r;
    }
  } else if (r != ZX_ERR_NOT_FOUND) {
    return r;
  }

  // Allocate the new name for the dnode, either by
  // (1) Stealing it from the previous dnode, if it used the same name, or
  // (2) Allocating a new name, if creating a new name.
  std::unique_ptr<char[]> namebuffer(nullptr);
  if (target_exists) {
    namebuffer = targetdn->TakeName();
    targetdn->Detach();
  } else {
    fbl::AllocChecker ac;
    namebuffer.reset(new (&ac) char[newname.length() + 1]);
    if (!ac.check()) {
      return ZX_ERR_NO_MEMORY;
    }
    memcpy(namebuffer.get(), newname.data(), newname.length());
    namebuffer[newname.length()] = '\0';
  }

  // NOTE:
  //
  // Validation ends here, and modifications begin. Rename should not fail
  // beyond this point.

  std::unique_ptr<Dnode> moved_node = olddn->RemoveFromParent();
  olddn->PutName(std::move(namebuffer), newname.length());
  Dnode::AddChild(newdir->dnode_, std::move(moved_node));
  return ZX_OK;
}

zx_status_t VnodeDir::Link(std::string_view name, fbl::RefPtr<fs::Vnode> target) {
  auto vn = fbl::RefPtr<Vnode>::Downcast(std::move(target));

  if (!IsDirectory()) {
    // Empty, unlinked parent
    return ZX_ERR_BAD_STATE;
  }

  if (vn->IsDirectory()) {
    // The target must not be a directory
    return ZX_ERR_NOT_FILE;
  }

  if (dnode_->Lookup(name, nullptr) == ZX_OK) {
    // The destination should not exist
    return ZX_ERR_ALREADY_EXISTS;
  }

  // Make a new dnode for the new name, attach the target vnode to it
  std::unique_ptr<Dnode> targetdn;
  if ((targetdn = Dnode::Create(name, vn)) == nullptr) {
    return ZX_ERR_NO_MEMORY;
  }

  // Attach the new dnode to its parent
  Dnode::AddChild(dnode_, std::move(targetdn));

  return ZX_OK;
}

zx_status_t VnodeDir::CreateFromVmo(std::string_view name, zx_handle_t vmo, zx_off_t off,
                                    zx_off_t len) {
  zx_status_t status;
  if ((status = CanCreate(name)) != ZX_OK) {
    return status;
  }

  std::lock_guard lock(mutex_);

  fbl::AllocChecker ac;
  fbl::RefPtr<Vnode> vn;
  vn = fbl::AdoptRef(new (&ac) VnodeVmo(vfs(), vmo, off, len));
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }
  if ((status = AttachVnode(vn, name, false)) != ZX_OK) {
    return status;
  }

  return ZX_OK;
}

zx_status_t VnodeDir::CanCreate(std::string_view name) const {
  if (!IsDirectory()) {
    return ZX_ERR_BAD_STATE;
  }
  zx_status_t status;
  if ((status = dnode_->Lookup(name, nullptr)) == ZX_ERR_NOT_FOUND) {
    return ZX_OK;
  } else if (status == ZX_OK) {
    return ZX_ERR_ALREADY_EXISTS;
  }
  return status;
}

zx_status_t VnodeDir::AttachVnode(const fbl::RefPtr<Vnode>& vn, std::string_view name, bool isdir) {
  // dnode takes a reference to the vnode
  std::unique_ptr<Dnode> dn;
  if ((dn = Dnode::Create(name, vn)) == nullptr) {
    return ZX_ERR_NO_MEMORY;
  }

  // Identify that the vnode is a directory (vn->dnode_ != nullptr) so that
  // addding a child will also increment the parent link_count (after all,
  // directories contain a ".." entry, which is a link to their parent).
  if (isdir) {
    vn->dnode_ = dn.get();
  }

  // parent takes first reference
  Dnode::AddChild(dnode_, std::move(dn));
  return ZX_OK;
}

}  // namespace memfs
