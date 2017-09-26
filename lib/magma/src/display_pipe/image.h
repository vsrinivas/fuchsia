// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_MAGMA_SRC_DISPLAY_PIPE_IMAGE_H_
#define GARNET_LIB_MAGMA_SRC_DISPLAY_PIPE_IMAGE_H_

#include "lib/images/fidl/image_info.fidl.h"
#include "garnet/lib/magma/src/display_pipe/magma_connection.h"

namespace display_pipe {
class Image {
 public:
  ~Image();

  // Returns nullptr on error.
  static std::unique_ptr<Image> Create(std::shared_ptr<MagmaConnection> conn,
                                       const scenic::ImageInfo &info,
                                       zx::vmo memory, uint64_t offset);
  magma_buffer_t buffer() { return buffer_; }

  void clean();

 private:
  Image();
  std::shared_ptr<MagmaConnection> conn_;
  scenic::ImageInfo info_;
  zx::vmo memory_;
  uint64_t offset_;

  magma_buffer_t buffer_;
};
}  // namespace display_pipe
#endif  // GARNET_LIB_MAGMA_SRC_DISPLAY_PIPE_IMAGE_H_
