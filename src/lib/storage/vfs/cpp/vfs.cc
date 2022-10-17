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
  }
  if (name == ".") {
    *out = std::move(vn);
    return ZX_OK;
  }
  return vn->Lookup(name, out);
}

// Validate open flags as much as they can be validated independently of the target node.
zx_status_t PrevalidateOptions(VnodeConnectionOptions options) {
  if (!options.rights.write && options.flags.truncate) {
    return ZX_ERR_INVALID_ARGS;
  }
  return ZX_OK;
}

}  // namespace

Vfs::Vfs() = default;

Vfs::OpenResult Vfs::Open(fbl::RefPtr<Vnode> vndir, std::string_view path,
                          VnodeConnectionOptions options, Rights parent_rights, uint32_t mode) {
  std::lock_guard lock(vfs_lock_);
  return OpenLocked(std::move(vndir), path, options, parent_rights, mode);
}

Vfs::OpenResult Vfs::OpenLocked(fbl::RefPtr<Vnode> vndir, std::string_view path,
                                VnodeConnectionOptions options, Rights parent_rights,
                                uint32_t mode) {
  FS_PRETTY_TRACE_DEBUG("VfsOpen: path='", path, "' options=", options);
  if (zx_status_t status = PrevalidateOptions(options); status != ZX_OK) {
    return status;
  }
  if (zx_status_t status = Vfs::Walk(vndir, path, &vndir, &path); status != ZX_OK) {
    return status;
  }

  if (vndir->IsRemote()) {
    // remote filesystem, return handle and path to caller
    return OpenResult::Remote{.vnode = std::move(vndir), .path = path};
  }

  {
    zx::result result = TrimName(path);
    if (result.is_error()) {
      return result.status_value();
    }
    if (path == "..") {
      return ZX_ERR_INVALID_ARGS;
    }
    if (result.value()) {
      options.flags.directory = true;
    }
  }

  fbl::RefPtr<Vnode> vn;
  bool just_created;
  if (options.flags.create) {
    zx::result result = EnsureExists(std::move(vndir), path, &vn, options, mode, parent_rights);
    if (result.is_error()) {
      return result.status_value();
    }
    just_created = result.value();
  } else {
    if (zx_status_t status = LookupNode(std::move(vndir), path, &vn); status != ZX_OK) {
      return status;
    }
    just_created = false;
  }

  if (vn->IsRemote()) {
    // Opening a mount point: Traverse across remote.
    return OpenResult::Remote{.vnode = std::move(vn), .path = "."};
  }

  if (!vn->IsService() && ReadonlyLocked() && options.rights.write) {
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
    return validated_options.status_value();
  }

  // |node_reference| requests that we don't actually open the underlying Vnode, but use the
  // connection as a reference to the Vnode.
  if (!options.flags.node_reference && !just_created) {
    if (zx_status_t status = OpenVnode(validated_options.value(), &vn); status != ZX_OK) {
      return status;
    }

    if (vn->IsRemote()) {
      // |OpenVnode| redirected us to a remote vnode; traverse across mount point.
      return OpenResult::Remote{.vnode = std::move(vn), .path = "."};
    }

    if (options.flags.truncate) {
      if (zx_status_t status = vn->Truncate(0); status != ZX_OK) {
        vn->Close();
        return status;
      }
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
  FS_PRETTY_TRACE_DEBUG("VfsTraversePathFetchVnode: path='", path, "'");
  if (zx_status_t result = Vfs::Walk(vndir, path, &vndir, &path); result != ZX_OK) {
    return result;
  }

  if (vndir->IsRemote()) {
    // remote filesystem, return handle and path to caller
    return TraversePathResult::Remote{.vnode = std::move(vndir), .path = path};
  }

  {
    zx::result result = TrimName(path);
    if (result.is_error()) {
      return result.status_value();
    }
    if (path == "..") {
      return ZX_ERR_INVALID_ARGS;
    }
  }

  fbl::RefPtr<Vnode> vn;
  if (zx_status_t status = LookupNode(std::move(vndir), path, &vn); status != ZX_OK) {
    return status;
  }

  if (vn->IsRemote()) {
    // Found a mount point: Traverse across remote.
    return TraversePathResult::Remote{.vnode = std::move(vn), .path = "."};
  }

  return TraversePathResult::Ok{.vnode = std::move(vn)};
}

zx_status_t Vfs::Unlink(fbl::RefPtr<Vnode> vndir, std::string_view name, bool must_be_dir) {
  {
    std::lock_guard lock(vfs_lock_);
    if (ReadonlyLocked()) {
      return ZX_ERR_ACCESS_DENIED;
    }
    if (zx_status_t status = vndir->Unlink(name, must_be_dir); status != ZX_OK) {
      return status;
    }
  }
  return ZX_OK;
}

zx::result<bool> Vfs::EnsureExists(fbl::RefPtr<Vnode> vndir, std::string_view path,
                                   fbl::RefPtr<Vnode>* out_vn, fs::VnodeConnectionOptions options,
                                   uint32_t mode, Rights parent_rights) {
  if (options.flags.directory && !S_ISDIR(mode)) {
    return zx::error(ZX_ERR_INVALID_ARGS);
  }
  if (options.flags.not_directory && S_ISDIR(mode)) {
    return zx::error(ZX_ERR_INVALID_ARGS);
  }
  if (ReadonlyLocked()) {
    return zx::error(ZX_ERR_ACCESS_DENIED);
  }
  if (!parent_rights.write) {
    return zx::error(ZX_ERR_ACCESS_DENIED);
  }
  zx_status_t status = path == "." ? ZX_ERR_ALREADY_EXISTS : vndir->Create(path, mode, out_vn);
  switch (status) {
    case ZX_OK:
      return zx::ok(true);
    case ZX_ERR_ALREADY_EXISTS:
      if (options.flags.fail_if_exists) {
        return zx::error(status);
      }
      __FALLTHROUGH;
    case ZX_ERR_NOT_SUPPORTED:
      // Filesystem may not support create (like devfs) in which case we should still try to open()
      // the file,
      return zx::make_result(LookupNode(std::move(vndir), path, out_vn), false);
    default:
      return zx::error(status);
  }
}

zx::result<bool> Vfs::TrimName(std::string_view& name) {
  bool is_dir = false;
  while (!name.empty() && name.back() == '/') {
    is_dir = true;
    name.remove_suffix(1);
  }

  if (name.empty()) {
    // 'name' should not contain paths consisting of exclusively '/' characters.
    return zx::error(ZX_ERR_INVALID_ARGS);
  }
  if (name.length() > NAME_MAX) {
    // Name must be less than the maximum-expected length.
    return zx::error(ZX_ERR_BAD_PATH);
  }
  if (name.find('/') != std::string::npos) {
    // Name must not contain '/' characters after being trimmed.
    return zx::error(ZX_ERR_INVALID_ARGS);
  }

  return zx::ok(is_dir);
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
      *out_vn = std::move(vn);
      *out_path = path;
      return ZX_OK;
    }

    if (zx_status_t status = LookupNode(std::move(vn), component, &vn); status != ZX_OK) {
      return status;
    }

    // Traverse to the next segment.
    path = path.substr(slash + 1);
  }
}

}  // namespace fs
