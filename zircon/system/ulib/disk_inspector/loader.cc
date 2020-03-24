// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "disk_inspector/loader.h"

#include <zircon/assert.h>

namespace disk_inspector {

zx_status_t Loader::RunReadOperation(storage::BlockBuffer* buffer, uint64_t buffer_offset,
                                     uint64_t dev_offset, uint64_t length) const {
  ZX_ASSERT(buffer->capacity() - buffer_offset >= length);
  storage::Operation operation{
      .type = storage::OperationType::kRead,
      .vmo_offset = buffer_offset,
      .dev_offset = dev_offset,
      .length = length,
  };
  return handler_->RunOperation(operation, buffer);
}

zx_status_t Loader::RunWriteOperation(storage::BlockBuffer* buffer, uint64_t buffer_offset,
                                      uint64_t dev_offset, uint64_t length) const {
  ZX_ASSERT(buffer->capacity() - buffer_offset >= length);
  storage::Operation operation{
      .type = storage::OperationType::kWrite,
      .vmo_offset = buffer_offset,
      .dev_offset = dev_offset,
      .length = length,
  };
  return handler_->RunOperation(operation, buffer);
}

}  // namespace disk_inspector
