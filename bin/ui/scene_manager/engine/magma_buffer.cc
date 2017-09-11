// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/ui/scene_manager/engine/magma_buffer.h"

#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

#include "lib/fxl/logging.h"
namespace scene_manager {

MagmaBuffer::MagmaBuffer() : magma_connection_(nullptr), buffer_(0) {}

MagmaBuffer::MagmaBuffer(MagmaConnection* conn, magma_buffer_t buffer)
    : magma_connection_(conn), buffer_(buffer) {}

MagmaBuffer::MagmaBuffer(MagmaBuffer&& rhs)
    : magma_connection_(rhs.magma_connection_), buffer_(rhs.buffer_) {
  rhs.magma_connection_ = nullptr;
  rhs.buffer_ = 0;
}

MagmaBuffer& MagmaBuffer::operator=(MagmaBuffer&& rhs) {
  magma_connection_ = rhs.magma_connection_;
  buffer_ = rhs.buffer_;
  rhs.magma_connection_ = nullptr;
  rhs.buffer_ = 0;
  return *this;
}

MagmaBuffer::~MagmaBuffer() {
  if (magma_connection_ != nullptr && buffer_ != 0) {
    magma_connection_->FreeBuffer(buffer_);
  }
}

MagmaBuffer MagmaBuffer::NewFromVmo(MagmaConnection* magma_connection,
                                    const zx::vmo& vmo) {
  magma_buffer_t buffer;
  bool success = magma_connection->ImportBuffer(vmo, &buffer);
  FXL_DCHECK(success);
  return success ? MagmaBuffer(magma_connection, buffer) : MagmaBuffer();
}

}  // namespace scene_manager
