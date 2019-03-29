// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FSL_SOCKET_BLOCKING_DRAIN_H_
#define LIB_FSL_SOCKET_BLOCKING_DRAIN_H_

#include <functional>

#include <lib/fit/function.h>
#include <lib/zx/socket.h>

#include "src/lib/fxl/fxl_export.h"

namespace fsl {

// Drain the given socket and call |write_bytes| with pieces of data.
// |write_bytes| must return the number of bytes consumed. Returns |true| if the
// socket has been drained, |false| if an error occured.
FXL_EXPORT bool BlockingDrainFrom(
    zx::socket source,
    fit::function<size_t(const void*, uint32_t)> write_bytes);

}  // namespace fsl

#endif  // LIB_FSL_SOCKET_BLOCKING_DRAIN_H_
