// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_MTL_SOCKET_BLOCKING_DRAIN_H_
#define LIB_MTL_SOCKET_BLOCKING_DRAIN_H_

#include <mx/socket.h>

#include <functional>

#include "lib/fxl/fxl_export.h"

namespace mtl {

// Drain the given socket and call |write_bytes| with pieces of data.
// |write_bytes| must return the number of bytes consumed. Returns |true| if the
// socket has been drained, |false| if an error occured.
FXL_EXPORT bool BlockingDrainFrom(
    mx::socket source,
    const std::function<size_t(const void*, uint32_t)>& write_bytes);

}  // namespace mtl

#endif  // LIB_MTL_SOCKET_BLOCKING_DRAIN_H_
