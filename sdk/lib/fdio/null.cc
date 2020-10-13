// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fdio/io.h>
#include <lib/zxio/inception.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

#include "internal.h"

zx_status_t fdio_default_get_token(fdio_t* io, zx_handle_t* out) { return ZX_ERR_NOT_SUPPORTED; }

zx_status_t fdio_default_get_attr(fdio_t* io, zxio_node_attributes_t* out) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t fdio_default_set_attr(fdio_t* io, const zxio_node_attributes_t* attr) {
  return ZX_ERR_NOT_SUPPORTED;
}

uint32_t fdio_default_convert_to_posix_mode(fdio_t* io, zxio_node_protocols_t protocols,
                                            zxio_abilities_t abilities) {
  return zxio_node_protocols_to_posix_type(protocols) |
         zxio_abilities_to_posix_permissions_for_file(abilities);
}

zx_status_t fdio_default_dirent_iterator_init(fdio_t* io, zxio_dirent_iterator_t* iterator,
                                              zxio_t* directory) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t fdio_default_dirent_iterator_next(fdio_t* io, zxio_dirent_iterator_t* iterator,
                                              zxio_dirent_t** out_entry) {
  return ZX_ERR_NOT_SUPPORTED;
}

void fdio_default_dirent_iterator_destroy(fdio_t* io, zxio_dirent_iterator_t* iterator) {}

zx_status_t fdio_default_unlink(fdio_t* io, const char* path, size_t len) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t fdio_default_truncate(fdio_t* io, off_t off) { return ZX_ERR_NOT_SUPPORTED; }

zx_status_t fdio_default_rename(fdio_t* io, const char* src, size_t srclen, zx_handle_t dst_token,
                                const char* dst, size_t dstlen) {
  zx_handle_close(dst_token);
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t fdio_default_link(fdio_t* io, const char* src, size_t srclen, zx_handle_t dst_token,
                              const char* dst, size_t dstlen) {
  zx_handle_close(dst_token);
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t fdio_default_get_flags(fdio_t* io, uint32_t* out_flags) { return ZX_ERR_NOT_SUPPORTED; }

zx_status_t fdio_default_set_flags(fdio_t* io, uint32_t flags) { return ZX_ERR_NOT_SUPPORTED; }

zx_status_t fdio_default_recvmsg(fdio_t* io, struct msghdr* msg, int flags, size_t* out_actual,
                                 int16_t* out_code) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t fdio_default_sendmsg(fdio_t* io, const struct msghdr* msg, int flags,
                                 size_t* out_actual, int16_t* out_code) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t fdio_default_open(fdio_t* io, const char* path, uint32_t flags, uint32_t mode,
                              fdio_t** out) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t fdio_default_clone(fdio_t* io, zx_handle_t* out_handle) { return ZX_ERR_NOT_SUPPORTED; }

zx_status_t fdio_default_unwrap(fdio_t* io, zx_handle_t* out_handle) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t fdio_default_borrow_channel(fdio_t* io, zx_handle_t* out_handle) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t fdio_default_bind(fdio_t* io, const struct sockaddr* addr, socklen_t addrlen,
                              int16_t* out_code) {
  *out_code = EBADF;
  return ZX_OK;
}

zx_status_t fdio_default_connect(fdio_t* io, const struct sockaddr* addr, socklen_t addrlen,
                                 int16_t* out_code) {
  *out_code = EBADF;
  return ZX_OK;
}

zx_status_t fdio_default_listen(fdio_t* io, int backlog, int16_t* out_code) {
  *out_code = EBADF;
  return ZX_OK;
}

zx_status_t fdio_default_getsockname(fdio_t* io, struct sockaddr* addr, socklen_t* addrlen,
                                     int16_t* out_code) {
  *out_code = EBADF;
  return ZX_OK;
}

zx_status_t fdio_default_getpeername(fdio_t* io, struct sockaddr* addr, socklen_t* addrlen,
                                     int16_t* out_code) {
  *out_code = EBADF;
  return ZX_OK;
}

zx_status_t fdio_default_getsockopt(fdio_t* io, int level, int optname, void* optval,
                                    socklen_t* optlen, int16_t* out_code) {
  *out_code = EBADF;
  return ZX_OK;
}

zx_status_t fdio_default_setsockopt(fdio_t* io, int level, int optname, const void* optval,
                                    socklen_t optlen, int16_t* out_code) {
  *out_code = EBADF;
  return ZX_OK;
}

zx_status_t fdio_default_shutdown(fdio_t* io, int how, int16_t* out_code) {
  return ZX_ERR_WRONG_TYPE;
}

void fdio_default_wait_begin(fdio_t* io, uint32_t events, zx_handle_t* handle,
                             zx_signals_t* _signals) {
  *handle = ZX_HANDLE_INVALID;
}

void fdio_default_wait_end(fdio_t* io, zx_signals_t signals, uint32_t* _events) {}

zx_status_t fdio_default_posix_ioctl(fdio_t* io, int req, va_list va) {
  return ZX_ERR_NOT_SUPPORTED;
}
