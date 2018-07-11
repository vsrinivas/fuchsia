// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>

#include <fbl/unique_fd.h>
#include <zircon/types.h>

namespace xdc {

// Requests a host xdc server stream that the client can read from or write to.
//
// If successful, returns ZX_OK and stores the stream file descriptor in out_fd.
// The client is in charge of closing this file descriptor once they are finished.
//
// Otherwise returns ZX_ERR_ALREADY_BOUND if the stream has already been claimed,
// or ZX_ERR_IO otherwise.
zx_status_t GetStream(uint32_t stream_id, fbl::unique_fd& out_fd);

}  // namespace xdc
