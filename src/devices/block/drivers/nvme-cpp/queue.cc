// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/block/drivers/nvme-cpp/queue.h"

#include <lib/ddk/debug.h>
#include <lib/zx/bti.h>
#include <zircon/syscalls.h>

namespace nvme {

zx::result<> Queue::Init(zx::unowned_bti bti, size_t entries) {
  size_t queue_size = entries * entry_size_;
  if (queue_size > zx_system_get_page_size()) {
    entries = zx_system_get_page_size() / entry_size_;
    queue_size = entries * entry_size_;
  }

  entry_count_ = entries;

  zx_status_t status = io_.Init(bti->get(), queue_size, IO_BUFFER_RW);
  if (status != ZX_OK) {
    return zx::error(status);
  }

  status = io_.PhysMap();
  if (status != ZX_OK) {
    return zx::error(status);
  }

  memset(io_.virt(), 0, io_.size());
  return zx::ok();
}

}  // namespace nvme
