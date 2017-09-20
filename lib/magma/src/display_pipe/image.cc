// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/magma/src/display_pipe/image.h"

#include "lib/fxl/logging.h"

namespace display_pipe {

Image::Image() = default;

Image::~Image() {
    conn_->FreeBuffer(buffer_);
}

std::unique_ptr<Image> Image::Create(std::shared_ptr<MagmaConnection> conn,
                                     const scenic::ImageInfo &info,
                                     zx::vmo memory, uint64_t offset) {
  if (offset != 0) {
      FXL_LOG(ERROR) << "Can't import an image with a non-zero offset.";
      return nullptr;
  }
  magma_buffer_t buffer;
  if (!conn->ImportBuffer(memory, &buffer)) {
      FXL_LOG(ERROR) << "Can't import buffer info magma.";
      return nullptr;
  }

  auto image = std::unique_ptr<Image>(new Image());
  image->conn_ = conn;
  image->info_ = info;
  image->memory_ = std::move(memory);
  image->offset_ = offset;
  image->buffer_ = buffer;

  return image;
}
}  // namespace display_pipe
