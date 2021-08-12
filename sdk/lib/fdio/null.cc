// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zxio/types.h>

#include "internal.h"

fdio::~fdio() = default;

zx::status<fdio_ptr> fdio::open(const char* path, uint32_t flags, uint32_t mode) {
  return zx::error(ZX_ERR_NOT_SUPPORTED);
}

zx_status_t fdio::add_inotify_filter(const char* path, uint32_t mask, uint32_t watch_descriptor,
                                     zx::socket socket) {
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
                                       zxio_dirent_t** out_entry) {
  return ZX_ERR_NOT_SUPPORTED;
}

void fdio::dirent_iterator_destroy(zxio_dirent_iterator_t* iterator) {}

zx_status_t fdio::unlink(const char* name, size_t len, int flags) { return ZX_ERR_NOT_SUPPORTED; }

zx_status_t fdio::truncate(uint64_t off) { return ZX_ERR_NOT_SUPPORTED; }

zx_status_t fdio::rename(const char* src, size_t srclen, zx_handle_t dst_token, const char* dst,
                         size_t dstlen) {
  zx_handle_close(dst_token);
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t fdio::link(const char* src, size_t srclen, zx_handle_t dst_token, const char* dst,
                       size_t dstlen) {
  zx_handle_close(dst_token);
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t fdio::get_flags(uint32_t* out_flags) { return ZX_ERR_NOT_SUPPORTED; }

zx_status_t fdio::set_flags(uint32_t flags) { return ZX_ERR_NOT_SUPPORTED; }

zx_status_t fdio::bind(const struct sockaddr* addr, socklen_t addrlen, int16_t* out_code) {
  *out_code = EBADF;
  return ZX_OK;
}

zx_status_t fdio::connect(const struct sockaddr* addr, socklen_t addrlen, int16_t* out_code) {
  *out_code = EBADF;
  return ZX_OK;
}

zx_status_t fdio::listen(int backlog, int16_t* out_code) {
  *out_code = EBADF;
  return ZX_OK;
}

zx_status_t fdio::accept(int flags, struct sockaddr* addr, socklen_t* addrlen,
                         zx_handle_t* out_handle, int16_t* out_code) {
  *out_code = EBADF;
  return ZX_OK;
}

zx_status_t fdio::getsockname(struct sockaddr* addr, socklen_t* addrlen, int16_t* out_code) {
  *out_code = EBADF;
  return ZX_OK;
}

zx_status_t fdio::getpeername(struct sockaddr* addr, socklen_t* addrlen, int16_t* out_code) {
  *out_code = EBADF;
  return ZX_OK;
}

zx_status_t fdio::getsockopt(int level, int optname, void* optval, socklen_t* optlen,
                             int16_t* out_code) {
  *out_code = EBADF;
  return ZX_OK;
}

zx_status_t fdio::setsockopt(int level, int optname, const void* optval, socklen_t optlen,
                             int16_t* out_code) {
  *out_code = EBADF;
  return ZX_OK;
}

zx_status_t fdio::recvmsg(struct msghdr* msg, int flags, size_t* out_actual, int16_t* out_code) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t fdio::sendmsg(const struct msghdr* msg, int flags, size_t* out_actual,
                          int16_t* out_code) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t fdio::shutdown(int how, int16_t* out_code) { return ZX_ERR_WRONG_TYPE; }

std::optional<fdio::last_reference> GetLastReference(fdio_ptr io) {
  if (io->IsLastReference()) {
    return std::make_optional<fdio::last_reference>(fbl::ExportToRawPtr(&io));
  }
  return std::nullopt;
}
