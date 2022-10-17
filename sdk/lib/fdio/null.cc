// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zxio/types.h>

#include "sdk/lib/fdio/internal.h"

fdio::~fdio() = default;

zx::result<fdio_ptr> fdio::open(std::string_view path, fuchsia_io::wire::OpenFlags flags,
                                uint32_t mode) {
  return zx::error(ZX_ERR_NOT_SUPPORTED);
}

zx_status_t fdio::add_inotify_filter(std::string_view path, uint32_t mask,
                                     uint32_t watch_descriptor, zx::socket socket) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t fdio::clone(zx_handle_t* out_handle) { return ZX_ERR_NOT_SUPPORTED; }

zx_status_t fdio::unwrap(zx_handle_t* out_handle) { return ZX_ERR_NOT_SUPPORTED; }

zx_status_t fdio::borrow_channel(zx_handle_t* out_handle) { return ZX_ERR_NOT_SUPPORTED; }

void fdio::wait_begin(uint32_t events, zx_handle_t* out_handle, zx_signals_t* out_signals) {
  *out_handle = ZX_HANDLE_INVALID;
}

void fdio::wait_end(zx_signals_t signals, uint32_t* out_events) {}

Errno fdio::posix_ioctl(int req, va_list va) { return Errno(ENOTTY); }

zx_status_t fdio::get_token(zx_handle_t* out) { return ZX_ERR_NOT_SUPPORTED; }

zx_status_t fdio::get_attr(zxio_node_attributes_t* out) { return ZX_ERR_NOT_SUPPORTED; }

zx_status_t fdio::set_attr(const zxio_node_attributes_t* attr) { return ZX_ERR_NOT_SUPPORTED; }

zx_status_t fdio::dirent_iterator_init(zxio_dirent_iterator_t* iterator, zxio_t* directory) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t fdio::dirent_iterator_next(zxio_dirent_iterator_t* iterator,
                                       zxio_dirent_t* inout_entry) {
  return ZX_ERR_NOT_SUPPORTED;
}

void fdio::dirent_iterator_destroy(zxio_dirent_iterator_t* iterator) {}

zx_status_t fdio::watch_directory(zxio_watch_directory_cb cb, zx_time_t deadline, void* context) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t fdio::unlink(std::string_view name, int flags) { return ZX_ERR_NOT_SUPPORTED; }

zx_status_t fdio::truncate(uint64_t off) { return ZX_ERR_NOT_SUPPORTED; }

zx_status_t fdio::rename(std::string_view src, zx_handle_t dst_token, std::string_view dst) {
  zx_handle_close(dst_token);
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t fdio::link(std::string_view src, zx_handle_t dst_token, std::string_view dst) {
  zx_handle_close(dst_token);
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t fdio::get_flags(fuchsia_io::wire::OpenFlags* out_flags) { return ZX_ERR_NOT_SUPPORTED; }

zx_status_t fdio::set_flags(fuchsia_io::wire::OpenFlags flags) { return ZX_ERR_NOT_SUPPORTED; }

zx_status_t fdio::recvmsg(struct msghdr* msg, int flags, size_t* out_actual, int16_t* out_code) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t fdio::sendmsg(const struct msghdr* msg, int flags, size_t* out_actual,
                          int16_t* out_code) {
  return ZX_ERR_NOT_SUPPORTED;
}

std::variant<fdio::last_reference, fdio_ptr> GetLastReference(fdio_ptr io) {
  if (io->IsLastReference()) {
    return fdio::last_reference(fbl::ExportToRawPtr(&io));
  }
  return std::move(io);
}
