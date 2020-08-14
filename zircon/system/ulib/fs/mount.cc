// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fdio/vfs.h>
#include <lib/zircon-internal/debug.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <threads.h>

#include <memory>
#include <utility>

#include <fbl/alloc_checker.h>
#include <fbl/auto_lock.h>
#include <fbl/intrusive_double_list.h>
#include <fbl/ref_ptr.h>
#include <fs/mount_channel.h>
#include <fs/vfs.h>
#include <fs/vfs_types.h>
#include <fs/vnode.h>

namespace fio = ::llcpp::fuchsia::io;

namespace fs {

constexpr Vfs::MountNode::MountNode() : vn_(nullptr) {}

Vfs::MountNode::~MountNode() { ZX_DEBUG_ASSERT(vn_ == nullptr); }

void Vfs::MountNode::SetNode(fbl::RefPtr<Vnode> vn) {
  ZX_DEBUG_ASSERT(vn_ == nullptr);
  vn_ = vn;
}

zx::channel Vfs::MountNode::ReleaseRemote() {
  ZX_DEBUG_ASSERT(vn_ != nullptr);
  zx::channel h = vn_->DetachRemote();
  vn_ = nullptr;
  return h;
}

bool Vfs::MountNode::VnodeMatch(fbl::RefPtr<Vnode> vn) const {
  ZX_DEBUG_ASSERT(vn_ != nullptr);
  return vn == vn_;
}

// Installs a remote filesystem on vn and adds it to the remote_list_.
zx_status_t Vfs::InstallRemote(fbl::RefPtr<Vnode> vn, MountChannel h) {
  if (vn == nullptr) {
    return ZX_ERR_ACCESS_DENIED;
  }

  // Allocate a node to track the remote handle
  fbl::AllocChecker ac;
  std::unique_ptr<MountNode> mount_point(new (&ac) MountNode());
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }
  zx_status_t status = vn->AttachRemote(std::move(h));
  if (status != ZX_OK) {
    return status;
  }
  // Save this node in the list of mounted vnodes
  mount_point->SetNode(std::move(vn));
  fbl::AutoLock lock(&vfs_lock_);
  remote_list_.push_front(std::move(mount_point));
  return ZX_OK;
}

// Installs a remote filesystem on vn and adds it to the remote_list_.
zx_status_t Vfs::InstallRemoteLocked(fbl::RefPtr<Vnode> vn, MountChannel h) {
  if (vn == nullptr) {
    return ZX_ERR_ACCESS_DENIED;
  }

  // Allocate a node to track the remote handle
  fbl::AllocChecker ac;
  std::unique_ptr<MountNode> mount_point(new (&ac) MountNode());
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }
  zx_status_t status = vn->AttachRemote(std::move(h));
  if (status != ZX_OK) {
    return status;
  }
  // Save this node in the list of mounted vnodes
  mount_point->SetNode(std::move(vn));
  remote_list_.push_front(std::move(mount_point));
  return ZX_OK;
}

zx_status_t Vfs::MountMkdir(fbl::RefPtr<Vnode> vn, fbl::StringPiece name, MountChannel h,
                            uint32_t flags) {
  fbl::AutoLock lock(&vfs_lock_);
  return OpenLocked(
             vn, name,
             fs::VnodeConnectionOptions::ReadOnly().set_create().set_directory().set_no_remote(),
             fs::Rights::ReadOnly(), S_IFDIR)
      .visit([&](auto&& result) FS_TA_REQUIRES(vfs_lock_) {
        using T = std::decay_t<decltype(result)>;
        using OpenResult = fs::Vfs::OpenResult;
        if constexpr (std::is_same_v<T, OpenResult::Error>) {
          return result;
        } else {
          if (result.vnode->IsRemote()) {
            if (flags & fio::MOUNT_CREATE_FLAG_REPLACE) {
              // There is an old remote handle on this vnode; shut it down and
              // replace it with our own.
              zx::channel old_remote;
              Vfs::UninstallRemoteLocked(vn, &old_remote);
              // Passing |zx::time::infinite_past()| results in a fire-and-forget call.
              // TODO(fxbug.dev/42264): Add proper tracking of remote filesystem teardown.
              Vfs::UnmountHandle(std::move(old_remote), zx::time::infinite_past());
            } else {
              return ZX_ERR_BAD_STATE;
            }
          }
          return Vfs::InstallRemoteLocked(result.vnode, std::move(h));
        }
      });
}

zx_status_t Vfs::UninstallRemote(fbl::RefPtr<Vnode> vn, zx::channel* h) {
  fbl::AutoLock lock(&vfs_lock_);
  return UninstallRemoteLocked(std::move(vn), h);
}

zx_status_t Vfs::ForwardOpenRemote(fbl::RefPtr<Vnode> vn, zx::channel channel,
                                   fbl::StringPiece path, VnodeConnectionOptions options,
                                   uint32_t mode) {
  fbl::AutoLock lock(&vfs_lock_);
  zx_handle_t h = vn->GetRemote();
  if (h == ZX_HANDLE_INVALID) {
    return ZX_ERR_NOT_FOUND;
  }

  auto r = fio::Directory::Call::Open(zx::unowned_channel(h), options.ToIoV1Flags(), mode,
                                      fidl::unowned_str(path), std::move(channel))
               .status();
  if (r == ZX_ERR_PEER_CLOSED) {
    zx::channel c;
    UninstallRemoteLocked(std::move(vn), &c);
  }
  return r;
}

// Uninstall the remote filesystem mounted on vn. Removes vn from the
// remote_list_, and sends its corresponding filesystem an 'unmount' signal.
zx_status_t Vfs::UninstallRemoteLocked(fbl::RefPtr<Vnode> vn, zx::channel* h) {
  std::unique_ptr<MountNode> mount_point;
  {
    mount_point =
        remote_list_.erase_if([&vn](const MountNode& node) { return node.VnodeMatch(vn); });
    if (!mount_point) {
      return ZX_ERR_NOT_FOUND;
    }
  }
  *h = mount_point->ReleaseRemote();
  return ZX_OK;
}

// Uninstall all remote filesystems. Acts like 'UninstallRemote' for all
// known remotes.
zx_status_t Vfs::UninstallAll(zx::time deadline) {
  std::unique_ptr<MountNode> mount_point;
  for (;;) {
    {
      fbl::AutoLock lock(&vfs_lock_);
      mount_point = remote_list_.pop_front();
    }
    if (mount_point) {
      Vfs::UnmountHandle(mount_point->ReleaseRemote(), deadline);
    } else {
      return ZX_OK;
    }
  }
}

}  // namespace fs
