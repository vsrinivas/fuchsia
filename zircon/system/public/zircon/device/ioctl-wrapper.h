// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SYSROOT_ZIRCON_DEVICE_IOCTL_WRAPPER_H_
#define SYSROOT_ZIRCON_DEVICE_IOCTL_WRAPPER_H_

#include <stddef.h>
#include <stdio.h>
#include <sys/types.h>
#include <zircon/compiler.h>

__BEGIN_CDECLS

extern ssize_t fdio_ioctl(int fd, int op, const void* in_buf, size_t in_len, void* out_buf,
                          size_t out_len);

#define IOCTL_WRAPPER_OUT(name, op, type)                            \
  static inline ssize_t name(int fd, type* out) {                    \
    return fdio_ioctl(fd, op, NULL, 0, out, out ? sizeof(*out) : 0); \
  }

#define IOCTL_WRAPPER_VAROUT(name, op, type)                      \
  static inline ssize_t name(int fd, type* out, size_t out_len) { \
    return fdio_ioctl(fd, op, NULL, 0, out, out_len);             \
  }

#define IOCTL_WRAPPER_INOUT(name, op, intype, outtype)                                \
  static inline ssize_t name(int fd, const intype* in, outtype* out) {                \
    return fdio_ioctl(fd, op, in, in ? sizeof(*in) : 0, out, out ? sizeof(*out) : 0); \
  }

__END_CDECLS

#endif  // SYSROOT_ZIRCON_DEVICE_IOCTL_WRAPPER_H_
