// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "magma/src/display_pipe/image.h"

#include "lib/ftl/logging.h"

namespace display_pipe {

Image::Image() = default;

Image::~Image() {
    conn_->FreeBuffer(buffer_);
}

std::unique_ptr<Image> Image::Create(std::shared_ptr<MagmaConnection> conn,
                                     const mozart2::ImageInfo &info,
                                     mx::vmo memory, uint64_t offset) {
  if (offset != 0) {
      FTL_LOG(ERROR) << "Can't import an image with a non-zero offset.";
      return nullptr;
  }
  magma_buffer_t buffer;
  if (!conn->ImportBuffer(memory, &buffer)) {
      FTL_LOG(ERROR) << "Can't import buffer info magma.";
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
