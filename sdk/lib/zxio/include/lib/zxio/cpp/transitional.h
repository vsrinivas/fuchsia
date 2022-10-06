// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ZXIO_INCLUDE_LIB_ZXIO_CPP_TRANSITIONAL_H_
#define LIB_ZXIO_INCLUDE_LIB_ZXIO_CPP_TRANSITIONAL_H_

#include <sys/socket.h>

#include <optional>

// This header exposes some shared functions between zxio and fdio as socket functionality moves
// from fdio into zxio.

std::optional<size_t> zxio_total_iov_len(const struct msghdr& msg);

size_t zxio_set_trunc_flags_and_return_out_actual(struct msghdr& msg, size_t written,
                                                  size_t truncated, int flags);

#endif  // LIB_ZXIO_INCLUDE_LIB_ZXIO_CPP_TRANSITIONAL_H_
