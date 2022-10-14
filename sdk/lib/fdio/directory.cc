// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <fidl/fuchsia.io/cpp/wire.h>
#include <lib/fdio/directory.h>

#include <fbl/auto_lock.h>

#include "sdk/lib/fdio/directory_internal.h"
#include "sdk/lib/fdio/fdio_unistd.h"
#include "sdk/lib/fdio/internal.h"
#include "sdk/lib/fdio/unistd.h"

namespace fio = fuchsia_io;

// TODO(https://fxbug.dev/101092): Shrink this to 0.
constexpr uint32_t kServiceFlags = static_cast<uint32_t>(fio::wire::OpenFlags::kRightReadable |
                                                         fio::wire::OpenFlags::kPosixWritable);

__EXPORT
zx_status_t fdio_service_connect(const char* path, zx_handle_t h) {
  zx::handle handle{h};
  fdio_ns_t* ns;
  if (zx_status_t status = fdio_ns_get_installed(&ns); status != ZX_OK) {
    return status;
  }
  return fdio_ns_open(ns, path, kServiceFlags, handle.release());
}

__EXPORT
zx_status_t fdio_service_connect_at(zx_handle_t dir, const char* path, zx_handle_t h) {
  return fdio_open_at(dir, path, kServiceFlags, h);
}

__EXPORT
zx_status_t fdio_service_connect_by_name(const char* name, zx_handle_t request) {
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
      status = fdio_service_connect("/svc", request.release());
    });
    if (status != ZX_OK) {
      return status;
    }
  }

  return fdio_service_connect_at(service_root.get(), name, request);
}

__EXPORT
zx_status_t fdio_open(const char* path, uint32_t flags, zx_handle_t request) {
  zx::handle handle{request};
  fdio_ns_t* ns;
  if (zx_status_t status = fdio_ns_get_installed(&ns); status != ZX_OK) {
    return status;
  }
  return fdio_ns_open(ns, path, flags, handle.release());
}

// We need to select some value to pass as the mode when calling Directory.Open. We use this value
// to match our historical behavior rather than for any more principled reason.
constexpr uint32_t kArbitraryMode =
    S_IRUSR | S_IWUSR | S_IXUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH;

namespace fdio_internal {

// TODO(https://fxbug.dev/97878): This should reuse the logic used by openat().
zx_status_t fdio_open_at(fidl::UnownedClientEnd<fio::Directory> directory, std::string_view path,
                         fuchsia_io::wire::OpenFlags flags, fidl::ServerEnd<fio::Node> request) {
  if (!directory.is_valid()) {
    return ZX_ERR_UNAVAILABLE;
  }

  return fidl::WireCall(directory)
      ->Open(flags, kArbitraryMode, fidl::StringView::FromExternal(path), std::move(request))
      .status();
}

}  // namespace fdio_internal

__EXPORT
zx_status_t fdio_open_at(zx_handle_t dir, const char* path, uint32_t flags,
                         zx_handle_t raw_request) {
  size_t length;
  zx_status_t status = fdio_validate_path(path, &length);
  if (status != ZX_OK) {
    return status;
  }

  fidl::UnownedClientEnd<fio::Directory> directory(dir);
  fidl::ServerEnd<fio::Node> request((zx::channel(raw_request)));
  auto fio_flags = static_cast<fio::wire::OpenFlags>(flags);

  return fdio_internal::fdio_open_at(directory, std::string_view(path, length), fio_flags,
                                     std::move(request));
}

namespace {

zx_status_t fdio_open_fd_at_internal(int dirfd, const char* dirty_path, fio::wire::OpenFlags flags,
                                     bool allow_absolute_path, int* out_fd) {
  // We're opening a file descriptor rather than just a channel (like fdio_open), so we always
  // want to Describe (or listen for an OnOpen event on) the opened connection. This ensures that
  // the fd is valid before returning from here, and mimics how open() and openat() behave
  // (fdio_flags_to_zxio always add _FLAG_DESCRIBE).
  flags |= fio::wire::OpenFlags::kDescribe;

  zx::status io = fdio_internal::open_at_impl(dirfd, dirty_path, flags, kArbitraryMode,
                                              {
                                                  .disallow_directory = false,
                                                  .allow_absolute_path = allow_absolute_path,
                                              });
  if (io.is_error()) {
    return io.status_value();
  }

  std::optional fd = bind_to_fd(io.value());
  if (!fd.has_value()) {
    return ZX_ERR_BAD_STATE;
  }
  *out_fd = fd.value();
  return ZX_OK;
}

}  // namespace

__EXPORT
zx_status_t fdio_open_fd(const char* path, uint32_t flags, int* out_fd) {
  return fdio_open_fd_at_internal(AT_FDCWD, path, static_cast<fio::wire::OpenFlags>(flags), true,
                                  out_fd);
}

__EXPORT
zx_status_t fdio_open_fd_at(int dirfd, const char* path, uint32_t flags, int* out_fd) {
  return fdio_open_fd_at_internal(dirfd, path, static_cast<fio::wire::OpenFlags>(flags), false,
                                  out_fd);
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
  fio::wire::OpenFlags flags = fio::wire::OpenFlags::kCloneSameRights;
  return fidl::WireCall(node)->Clone(flags, std::move(request)).status();
}
