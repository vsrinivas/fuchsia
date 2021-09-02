// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/storage/vfs/cpp/vfs.h"

#include <lib/fdio/watcher.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <memory>
#include <string_view>
#include <utility>
#include <vector>

#include "src/lib/storage/vfs/cpp/debug.h"
#include "src/lib/storage/vfs/cpp/vnode.h"

namespace fs {
namespace {

zx_status_t LookupNode(fbl::RefPtr<Vnode> vn, std::string_view name, fbl::RefPtr<Vnode>* out) {
  if (name == "..") {
    return ZX_ERR_INVALID_ARGS;
  } else if (name == ".") {
    *out = std::move(vn);
    return ZX_OK;
  }
  return vn->Lookup(name, out);
}

// Validate open flags as much as they can be validated independently of the target node.
zx_status_t PrevalidateOptions(VnodeConnectionOptions options) {
  if (!options.rights.write) {
    if (options.flags.truncate) {
      return ZX_ERR_INVALID_ARGS;
    }
  } else if (!options.rights.any()) {
    if (!options.flags.node_reference) {
      return ZX_ERR_INVALID_ARGS;
    }
  }
  return ZX_OK;
}

}  // namespace

Vfs::Vfs() = default;

Vfs::~Vfs() {
  // Keep owning references to each vnode in case the callbacks cause any nodes to be deleted.
  std::vector<fbl::RefPtr<Vnode>> nodes_to_notify;
  {
    // This lock should not be necessary since the destructor should be single-threaded but is good
    // for completeness.
    std::lock_guard lock(vfs_lock_);
    nodes_to_notify.reserve(live_nodes_.size());
    for (auto& node_ptr : live_nodes_)
      nodes_to_notify.push_back(fbl::RefPtr<Vnode>(node_ptr));
  }

  // Notify all nodes that we're getting deleted.
  for (auto& node : nodes_to_notify)
    node->WillDestroyVfs();
}

Vfs::OpenResult Vfs::Open(fbl::RefPtr<Vnode> vndir, std::string_view path,
                          VnodeConnectionOptions options, Rights parent_rights, uint32_t mode) {
  std::lock_guard lock(vfs_lock_);
  return OpenLocked(std::move(vndir), path, options, parent_rights, mode);
}

Vfs::OpenResult Vfs::OpenLocked(fbl::RefPtr<Vnode> vndir, std::string_view path,
                                VnodeConnectionOptions options, Rights parent_rights,
                                uint32_t mode) {
  FS_PRETTY_TRACE_DEBUG("VfsOpen: path='", Path(path.data(), path.size()), "' options=", options);
  zx_status_t r;
  if ((r = PrevalidateOptions(options)) != ZX_OK) {
    return r;
  }
  if ((r = Vfs::Walk(vndir, path, &vndir, &path)) < 0) {
    return r;
  }

  if (vndir->IsRemote()) {
    // remote filesystem, return handle and path to caller
    return OpenResult::Remote{.vnode = std::move(vndir), .path = path};
  }

  {
    bool must_be_dir = false;
    if ((r = TrimName(path, &path, &must_be_dir)) != ZX_OK) {
      return r;
    } else if (path == "..") {
      return ZX_ERR_INVALID_ARGS;
    }
    if (must_be_dir) {
      options.flags.directory = true;
    }
  }

  fbl::RefPtr<Vnode> vn;
  bool just_created = false;
  if (options.flags.create) {
    if ((r = EnsureExists(std::move(vndir), path, &vn, options, mode, parent_rights,
                          &just_created)) != ZX_OK) {
      return r;
    }
  } else {
    if ((r = LookupNode(std::move(vndir), path, &vn)) != ZX_OK) {
      return r;
    }
  }

  if (!options.flags.no_remote && vn->IsRemote()) {
    // Opening a mount point: Traverse across remote.
    return OpenResult::RemoteRoot{.vnode = std::move(vn)};
  }

  if (ReadonlyLocked() && options.rights.write) {
    return ZX_ERR_ACCESS_DENIED;
  }

  if (vn->Supports(fs::VnodeProtocol::kDirectory) &&
      (options.flags.posix_write || options.flags.posix_execute)) {
    // This is such that POSIX open() can open a directory with O_RDONLY, and still get the
    // write/execute right if the parent directory connection has the write/execute right
    // respectively.  With the execute right in particular, the resulting connection may be passed
    // to fdio_get_vmo_exec() which requires the execute right. This transfers write and execute
    // from the parent, if present.
    Rights inheritable_rights{};
    inheritable_rights.write = options.flags.posix_write;
    inheritable_rights.execute = options.flags.posix_execute;
    options.rights |= parent_rights & inheritable_rights;
  }
  auto validated_options = vn->ValidateOptions(options);
  if (validated_options.is_error()) {
    return validated_options.error();
  }

  // |node_reference| requests that we don't actually open the underlying Vnode, but use the
  // connection as a reference to the Vnode.
  if (!options.flags.node_reference && !just_created) {
    if ((r = OpenVnode(validated_options.value(), &vn)) != ZX_OK) {
      return r;
    }

    if (!options.flags.no_remote && vn->IsRemote()) {
      // |OpenVnode| redirected us to a remote vnode; traverse across mount point.
      return OpenResult::RemoteRoot{.vnode = std::move(vn)};
    }

    if (options.flags.truncate && ((r = vn->Truncate(0)) < 0)) {
      vn->Close();
      return r;
    }
  }

  return OpenResult::Ok{.vnode = std::move(vn), .validated_options = validated_options.value()};
}

Vfs::TraversePathResult Vfs::TraversePathFetchVnode(fbl::RefPtr<Vnode> vndir,
                                                    std::string_view path) {
  std::lock_guard lock(vfs_lock_);
  return TraversePathFetchVnodeLocked(std::move(vndir), path);
}

Vfs::TraversePathResult Vfs::TraversePathFetchVnodeLocked(fbl::RefPtr<Vnode> vndir,
                                                          std::string_view path) {
  FS_PRETTY_TRACE_DEBUG("VfsTraversePathFetchVnode: path='", Path(path.data(), path.size()));
  if (zx_status_t result = Vfs::Walk(vndir, path, &vndir, &path); result != ZX_OK) {
    return result;
  }

  if (vndir->IsRemote()) {
    // remote filesystem, return handle and path to caller
    return TraversePathResult::Remote{.vnode = std::move(vndir), .path = path};
  }

  zx_status_t result;
  {
    bool must_be_dir = false;
    if ((result = TrimName(path, &path, &must_be_dir)) != ZX_OK) {
      return result;
    } else if (path == "..") {
      return ZX_ERR_INVALID_ARGS;
    }
  }

  fbl::RefPtr<Vnode> vn;
  if ((result = LookupNode(std::move(vndir), path, &vn)) != ZX_OK) {
    return result;
  }

  if (vn->IsRemote()) {
    // Found a mount point: Traverse across remote.
    return TraversePathResult::RemoteRoot{.vnode = std::move(vn)};
  }

  return TraversePathResult::Ok{.vnode = std::move(vn)};
}

zx_status_t Vfs::Unlink(fbl::RefPtr<Vnode> vndir, std::string_view name, bool must_be_dir) {
  {
    std::lock_guard lock(vfs_lock_);
    if (ReadonlyLocked()) {
      return ZX_ERR_ACCESS_DENIED;
    } else {
      if (zx_status_t status = vndir->Unlink(name, must_be_dir); status != ZX_OK) {
        return status;
      }
    }
  }
  return ZX_OK;
}

void Vfs::RegisterVnode(Vnode* vnode) {
  std::lock_guard lock(live_nodes_lock_);

  // Should not be registered twice.
  ZX_DEBUG_ASSERT(live_nodes_.find(vnode) == live_nodes_.end());
  live_nodes_.insert(vnode);
}

void Vfs::UnregisterVnode(Vnode* vnode) {
  std::lock_guard lock(live_nodes_lock_);
  UnregisterVnodeLocked(vnode);
}

void Vfs::UnregisterVnodeLocked(Vnode* vnode) {
  auto found = live_nodes_.find(vnode);
  ZX_DEBUG_ASSERT(found != live_nodes_.end());  // Should always be registered first.
  live_nodes_.erase(found);
}

zx_status_t Vfs::EnsureExists(fbl::RefPtr<Vnode> vndir, std::string_view path,
                              fbl::RefPtr<Vnode>* out_vn, fs::VnodeConnectionOptions options,
                              uint32_t mode, Rights parent_rights, bool* did_create) {
  zx_status_t status;
  if (options.flags.directory && !S_ISDIR(mode)) {
    return ZX_ERR_INVALID_ARGS;
  } else if (options.flags.not_directory && S_ISDIR(mode)) {
    return ZX_ERR_INVALID_ARGS;
  } else if (path == ".") {
    return ZX_ERR_INVALID_ARGS;
  } else if (ReadonlyLocked()) {
    return ZX_ERR_ACCESS_DENIED;
  } else if (!parent_rights.write) {
    return ZX_ERR_ACCESS_DENIED;
  }
  if ((status = vndir->Create(path, mode, out_vn)) != ZX_OK) {
    *did_create = false;
    if ((status == ZX_ERR_ALREADY_EXISTS) && !options.flags.fail_if_exists) {
      return LookupNode(std::move(vndir), path, out_vn);
    }
    if (status == ZX_ERR_NOT_SUPPORTED) {
      // Filesystem may not support create (like devfs) in which case we should still try to open()
      // the file,
      return LookupNode(std::move(vndir), path, out_vn);
    }
    return status;
  }

  *did_create = true;
  return ZX_OK;
}

zx_status_t Vfs::TrimName(std::string_view name, std::string_view* name_out, bool* is_dir_out) {
  *is_dir_out = false;

  size_t len = name.length();
  while ((len > 0) && name[len - 1] == '/') {
    len--;
    *is_dir_out = true;
  }

  if (len == 0) {
    // 'name' should not contain paths consisting of exclusively '/' characters.
    return ZX_ERR_INVALID_ARGS;
  } else if (len > NAME_MAX) {
    // Name must be less than the maximum-expected length.
    return ZX_ERR_BAD_PATH;
  } else if (memchr(name.data(), '/', len) != nullptr) {
    // Name must not contain '/' characters after being trimmed.
    return ZX_ERR_INVALID_ARGS;
  }

  *name_out = std::string_view(name.data(), len);
  return ZX_OK;
}

zx_status_t Vfs::Readdir(Vnode* vn, VdirCookie* cookie, void* dirents, size_t len,
                         size_t* out_actual) {
  std::lock_guard lock(vfs_lock_);
  return vn->Readdir(cookie, dirents, len, out_actual);
}

void Vfs::SetReadonly(bool value) {
  std::lock_guard lock(vfs_lock_);
  readonly_ = value;
}

zx_status_t Vfs::Walk(fbl::RefPtr<Vnode> vn, std::string_view path, fbl::RefPtr<Vnode>* out_vn,
                      std::string_view* out_path) {
  zx_status_t r;

  if (path.empty()) {
    return ZX_ERR_INVALID_ARGS;
  }

  // Handle "." and "/".
  if (path == "." || path == "/") {
    *out_vn = std::move(vn);
    *out_path = ".";
    return ZX_OK;
  }

  // Allow leading '/'.
  if (path[0] == '/') {
    path = path.substr(1);
  }

  // Allow trailing '/', but only if preceded by something.
  if (path.length() > 1 && path.back() == '/') {
    path = path.substr(0, path.length() - 1);
  }

  for (;;) {
    if (vn->IsRemote()) {
      // Remote filesystem mount, caller must resolve.
      *out_vn = std::move(vn);
      *out_path = path;
      return ZX_OK;
    }

    // Look for the next '/' separated path component.
    size_t slash = path.find('/');
    std::string_view component = path.substr(0, slash);
    if (component.length() > NAME_MAX) {
      return ZX_ERR_BAD_PATH;
    }
    if (component.empty() || component == "." || component == "..") {
      return ZX_ERR_INVALID_ARGS;
    }

    if (slash == std::string_view::npos) {
      // Final path segment.
      *out_vn = vn;
      *out_path = path;
      return ZX_OK;
    }

    if ((r = LookupNode(std::move(vn), component, &vn)) != ZX_OK) {
      return r;
    }

    // Traverse to the next segment.
    path = path.substr(slash + 1);
  }
}

}  // namespace fs
