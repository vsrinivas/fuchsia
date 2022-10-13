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

void zxio_wait_begin_inner(zxio_t* io, uint32_t events, zx_signals_t signals,
                           zx_handle_t* out_handle, zx_signals_t* out_signals);

void zxio_wait_end_inner(zxio_t* io, zx_signals_t signals, uint32_t* out_events,
                         zx_signals_t* out_signals);

#endif  // LIB_ZXIO_INCLUDE_LIB_ZXIO_CPP_TRANSITIONAL_H_
