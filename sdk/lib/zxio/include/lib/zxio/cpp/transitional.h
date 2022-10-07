// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ZXIO_INCLUDE_LIB_ZXIO_CPP_TRANSITIONAL_H_
#define LIB_ZXIO_INCLUDE_LIB_ZXIO_CPP_TRANSITIONAL_H_

#include <lib/zxio/types.h>
#include <sys/socket.h>
#include <zircon/types.h>

// This header exposes some shared functions between zxio and fdio as socket functionality moves
// from fdio into zxio.

zx_status_t zxio_sendmsg_inner(zxio_t* io, const struct msghdr* msg, int flags, size_t* out_actual);

zx_status_t zxio_recvmsg_inner(zxio_t* io, struct msghdr* msg, int flags, size_t* out_actual);

#endif  // LIB_ZXIO_INCLUDE_LIB_ZXIO_CPP_TRANSITIONAL_H_
