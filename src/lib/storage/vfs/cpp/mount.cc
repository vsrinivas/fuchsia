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
#include <string_view>
#include <utility>

#include <fbl/alloc_checker.h>
#include <fbl/auto_lock.h>
#include <fbl/intrusive_double_list.h>
#include <fbl/ref_ptr.h>

#include "src/lib/storage/vfs/cpp/fuchsia_vfs.h"
#include "src/lib/storage/vfs/cpp/mount_channel.h"
#include "src/lib/storage/vfs/cpp/vfs_types.h"
#include "src/lib/storage/vfs/cpp/vnode.h"

namespace fio = fuchsia_io;

namespace fs {

constexpr FuchsiaVfs::MountNode::MountNode() = default;

FuchsiaVfs::MountNode::~MountNode() { ZX_DEBUG_ASSERT(vn_ == nullptr); }

void FuchsiaVfs::MountNode::SetNode(fbl::RefPtr<Vnode> vn) {
  ZX_DEBUG_ASSERT(vn_ == nullptr);
  vn_ = vn;
}

fidl::ClientEnd<fio::Directory> FuchsiaVfs::MountNode::ReleaseRemote() {
  ZX_DEBUG_ASSERT(vn_ != nullptr);
  fidl::ClientEnd<fio::Directory> h = vn_->DetachRemote();
  vn_ = nullptr;
  return h;
}

bool FuchsiaVfs::MountNode::VnodeMatch(fbl::RefPtr<Vnode> vn) const {
  ZX_DEBUG_ASSERT(vn_ != nullptr);
  return vn == vn_;
}

// Installs a remote filesystem on vn and adds it to the remote_list_.
zx_status_t FuchsiaVfs::InstallRemote(fbl::RefPtr<Vnode> vn, MountChannel h) {
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
  std::lock_guard lock(vfs_lock_);
  remote_list_.push_front(std::move(mount_point));
  return ZX_OK;
}

// Installs a remote filesystem on vn and adds it to the remote_list_.
zx_status_t FuchsiaVfs::InstallRemoteLocked(fbl::RefPtr<Vnode> vn, MountChannel h) {
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

zx_status_t FuchsiaVfs::MountMkdir(fbl::RefPtr<Vnode> vn, std::string_view name, MountChannel h,
                                   uint32_t flags) {
  std::lock_guard lock(vfs_lock_);
  return OpenLocked(
             vn, name,
             fs::VnodeConnectionOptions::ReadOnly().set_create().set_directory().set_no_remote(),
             fs::Rights::ReadWrite(), S_IFDIR)
      .visit([&](auto&& result) __TA_REQUIRES(vfs_lock_) {
        using T = std::decay_t<decltype(result)>;
        using OpenResult = fs::FuchsiaVfs::OpenResult;
        if constexpr (std::is_same_v<T, OpenResult::Error>) {
          return result;
        } else {
          if (result.vnode->IsRemote()) {
            if (flags & fio::wire::kMountCreateFlagReplace) {
              // There is an old remote handle on this vnode; shut it down and replace it with our
              // own.
              fidl::ClientEnd<fio::Directory> old_remote;
              FuchsiaVfs::UninstallRemoteLocked(vn, &old_remote);
              // Passing |zx::time::infinite_past()| results in a fire-and-forget call.
              // TODO(fxbug.dev/42264): Add proper tracking of remote filesystem teardown.
              // Note: this is best-effort, and would fail if the remote endpoint does not speak the
              // |fuchsia.io/DirectoryAdmin| protocol.
              fidl::ClientEnd<fio::DirectoryAdmin> old_remote_admin(old_remote.TakeChannel());
              FuchsiaVfs::UnmountHandle(std::move(old_remote_admin), zx::time::infinite_past());
            } else {
              return ZX_ERR_BAD_STATE;
            }
          }
          return FuchsiaVfs::InstallRemoteLocked(result.vnode, std::move(h));
        }
      });
}

zx_status_t FuchsiaVfs::UninstallRemote(fbl::RefPtr<Vnode> vn, fidl::ClientEnd<fio::Directory>* h) {
  std::lock_guard lock(vfs_lock_);
  return UninstallRemoteLocked(std::move(vn), h);
}

zx_status_t FuchsiaVfs::ForwardOpenRemote(fbl::RefPtr<Vnode> vn, fidl::ServerEnd<fio::Node> channel,
                                          std::string_view path, VnodeConnectionOptions options,
                                          uint32_t mode) {
  std::lock_guard lock(vfs_lock_);
  auto h = vn->GetRemote();
  if (!h.is_valid()) {
    return ZX_ERR_NOT_FOUND;
  }

  auto r = fidl::WireCall(h)
               .Open(options.ToIoV1Flags(), mode, fidl::StringView::FromExternal(path),
                     std::move(channel))
               .status();
  if (r == ZX_ERR_PEER_CLOSED) {
    fidl::ClientEnd<fio::Directory> c;
    UninstallRemoteLocked(std::move(vn), &c);
  }
  return r;
}

// Uninstall the remote filesystem mounted on vn. Removes vn from the remote_list_, and sends its
// corresponding filesystem an 'unmount' signal.
zx_status_t FuchsiaVfs::UninstallRemoteLocked(fbl::RefPtr<Vnode> vn,
                                              fidl::ClientEnd<fio::Directory>* h) {
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

// Uninstall all remote filesystems. Acts like 'UninstallRemote' for all known remotes.
zx_status_t FuchsiaVfs::UninstallAll(zx::time deadline) {
  std::unique_ptr<MountNode> mount_point;
  for (;;) {
    {
      std::lock_guard lock(vfs_lock_);
      mount_point = remote_list_.pop_front();
    }
    if (mount_point) {
      // Note: this is best-effort, and would fail if the remote endpoint does not speak the
      // |fuchsia.io/DirectoryAdmin| protocol.
      fidl::ClientEnd<fio::DirectoryAdmin> mount_admin(mount_point->ReleaseRemote().TakeChannel());
      FuchsiaVfs::UnmountHandle(std::move(mount_admin), deadline);
    } else {
      return ZX_OK;
    }
  }
}

}  // namespace fs
