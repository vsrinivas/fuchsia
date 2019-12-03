// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_LIB_SOCKET_BLOCKING_DRAIN_H_
#define SRC_LEDGER_LIB_SOCKET_BLOCKING_DRAIN_H_

#include <lib/fit/function.h>
#include <lib/zx/socket.h>

#include <functional>

namespace ledger {

// Drain the given socket and call |write_bytes| with pieces of data.
// |write_bytes| must return the number of bytes consumed. Returns |true| if the
// socket has been drained, |false| if an error occured.
bool BlockingDrainFrom(zx::socket source, fit::function<size_t(const void*, uint32_t)> write_bytes);

}  // namespace ledger

#endif  // SRC_LEDGER_LIB_SOCKET_BLOCKING_DRAIN_H_
