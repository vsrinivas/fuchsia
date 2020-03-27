// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "block-client/cpp/block-device.h"

namespace block_client {

zx_status_t BlockDevice::BlockDetachVmo(storage::Vmoid vmoid) {
  if (!vmoid.IsAttached()) {
    return ZX_OK;
  }
  block_fifo_request_t request = {};
  request.opcode = BLOCKIO_CLOSE_VMO;
  request.vmoid = vmoid.TakeId();
  return FifoTransaction(&request, 1);
}

}  // namespace block_client
