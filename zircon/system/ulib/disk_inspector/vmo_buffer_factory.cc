// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "disk_inspector/vmo_buffer_factory.h"

#include <storage/buffer/vmo_buffer.h>

namespace disk_inspector {

fit::result<std::unique_ptr<storage::BlockBuffer>, zx_status_t> VmoBufferFactory::CreateBuffer(
    size_t capacity) const {
  auto buffer = std::make_unique<storage::VmoBuffer>();
  zx_status_t status = buffer->Initialize(registry_, capacity, block_size_, "factory-vmo-buffer");
  if (status != ZX_OK) {
    return fit::error(status);
  }
  return fit::ok(std::move(buffer));
}

}  // namespace disk_inspector
