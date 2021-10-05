// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.io/cpp/wire.h>
#include <lib/fdio/directory.h>
#include <zircon/device/vfs.h>

#include <fbl/auto_lock.h>

#include "fdio_unistd.h"
#include "internal.h"

namespace fio = fuchsia_io;

__EXPORT
zx_status_t fdio_service_connect(const char* path, zx_handle_t h) {
  return fdio_open(path, ZX_FS_RIGHT_READABLE | ZX_FS_RIGHT_WRITABLE, h);
}

__EXPORT
zx_status_t fdio_service_connect_at(zx_handle_t dir, const char* path, zx_handle_t h) {
  return fdio_open_at(dir, path, ZX_FS_RIGHT_READABLE | ZX_FS_RIGHT_WRITABLE, h);
}

__EXPORT
zx_status_t fdio_service_connect_by_name(const char name[], zx_handle_t request) {
  static zx::channel service_root;

  {
    static std::once_flag once;
    static zx_status_t status;
    std::call_once(once, [&]() {
      zx::channel request;
      status = zx::channel::create(0, &service_root, &request);
      if (status != ZX_OK) {
        return;
      }
      // TODO(abarth): Use "/svc/" once that actually works.
      status = fdio_service_connect("/svc/.", request.release());
    });
    if (status != ZX_OK) {
      return status;
    }
  }

  return fdio_service_connect_at(service_root.get(), name, request);
}

__EXPORT
zx_status_t fdio_open(const char* path, uint32_t flags, zx_handle_t request) {
  auto handle = zx::handle(request);
  // TODO: fdio_validate_path?
  if (path == nullptr) {
    return ZX_ERR_INVALID_ARGS;
  }
  // Otherwise attempt to connect through the root namespace
  fdio_ns_t* ns;
  zx_status_t status = fdio_ns_get_installed(&ns);
  if (status != ZX_OK) {
    return status;
  }
  return fdio_ns_connect(ns, path, flags, handle.release());
}

// We need to select some value to pass as the mode when calling Directory.Open. We use this value
// to match our historical behavior rather than for any more principled reason.
constexpr uint32_t kArbitraryMode =
    S_IRUSR | S_IWUSR | S_IXUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH;

__EXPORT
zx_status_t fdio_open_at(zx_handle_t dir, const char* path, uint32_t flags,
                         zx_handle_t raw_request) {
  auto request = fidl::ServerEnd<fio::Node>(zx::channel(raw_request));
  auto directory = fidl::UnownedClientEnd<fio::Directory>(dir);
  if (!directory.is_valid()) {
    return ZX_ERR_UNAVAILABLE;
  }

  size_t length;
  zx_status_t status = fdio_validate_path(path, &length);
  if (status != ZX_OK) {
    return status;
  }

  if (flags & ZX_FS_FLAG_DESCRIBE) {
    return ZX_ERR_INVALID_ARGS;
  }

  return fidl::WireCall(directory)
      ->Open(flags, kArbitraryMode, fidl::StringView::FromExternal(path, length),
             std::move(request))
      .status();
}

namespace {

zx_status_t fdio_open_fd_common(const fdio_ptr& iodir, const char* path, uint32_t flags,
                                uint32_t mode, int* out_fd) {
  // We're opening a file descriptor rather than just a channel (like fdio_open), so we always want
  // to Describe (or listen for an OnOpen event on) the opened connection. This ensures that the fd
  // is valid before returning from here, and mimics how open() and openat() behave
  // (fdio_flags_to_zxio always add _FLAG_DESCRIBE).
  flags |= ZX_FS_FLAG_DESCRIBE;

  zx::status io = iodir->open(path, flags, mode);
  if (io.is_error()) {
    return io.status_value();
  }

  std::optional fd = bind_to_fd(io.value());
  if (fd.has_value()) {
    *out_fd = fd.value();
    return ZX_OK;
  }
  return ZX_ERR_BAD_STATE;
}

}  // namespace

__EXPORT
zx_status_t fdio_open_fd(const char* path, uint32_t flags, int* out_fd) {
  zx_status_t status = fdio_validate_path(path, nullptr);
  if (status != ZX_OK) {
    return status;
  }

  // Since we are sending a request to the root handle, require that we start at '/'. (In fdio_open
  // above this is done by fdio_ns_connect.)
  if (path[0] != '/') {
    return ZX_ERR_NOT_FOUND;
  }
  path++;

  return fdio_open_fd_common(
      []() {
        fbl::AutoLock lock(&fdio_lock);
        return fdio_root_handle.get();
      }(),
      path, flags, kArbitraryMode, out_fd);
}

__EXPORT
zx_status_t fdio_open_fd_at(int dir_fd, const char* path, uint32_t flags, int* out_fd) {
  zx_status_t status = fdio_validate_path(path, nullptr);
  if (status != ZX_OK) {
    return status;
  }

  fdio_ptr iodir = fd_to_io(dir_fd);
  if (iodir == nullptr) {
    return ZX_ERR_INVALID_ARGS;
  }

  return fdio_open_fd_common(iodir, path, flags, kArbitraryMode, out_fd);
}

__EXPORT
zx_handle_t fdio_service_clone(zx_handle_t handle) {
  zx::status endpoints = fidl::CreateEndpoints<fio::Node>();
  if (endpoints.is_error()) {
    return ZX_HANDLE_INVALID;
  }
  zx_status_t status = fdio_service_clone_to(handle, endpoints->server.channel().release());
  if (status != ZX_OK) {
    return ZX_HANDLE_INVALID;
  }
  return endpoints->client.channel().release();
}

__EXPORT
zx_status_t fdio_service_clone_to(zx_handle_t handle, zx_handle_t request_raw) {
  auto request = fidl::ServerEnd<fio::Node>(zx::channel(request_raw));
  auto node = fidl::UnownedClientEnd<fio::Node>(handle);
  if (!node.is_valid()) {
    return ZX_ERR_INVALID_ARGS;
  }
  uint32_t flags = ZX_FS_FLAG_CLONE_SAME_RIGHTS;
  return fidl::WireCall(node)->Clone(flags, std::move(request)).status();
}
