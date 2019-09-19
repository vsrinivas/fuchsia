// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_CALIBRATION_IMAGE_WRITER_IMAGE_WRITER_H_
#define SRC_CAMERA_CALIBRATION_IMAGE_WRITER_IMAGE_WRITER_H_

#include <lib/fzl/vmo-pool.h>
#include <stdint.h>

#include <src/camera/drivers/isp/modules/dma-format.h>

namespace camera {

class ImageWriter {
 public:
  // Constructor must be public to be instantiated with a unique pointer
  explicit ImageWriter(size_t vmo_size) : vmo_size_(vmo_size) {}

  // Factory method that outputs an ImageWriter with all its params set for the corresponding
  // format.
  static std::unique_ptr<ImageWriter> Create(uint32_t width, uint32_t height,
                                             camera::DmaFormat::PixelType pixel_format);

  // Creates a vmo of appropriate size (depending on DmaFormat image size) and passes it to be
  // filled by specified format handler.
  zx_status_t CreateImage(zx::vmo* vmo);

  size_t VmoSize() const { return vmo_size_; }

 private:
  // Fills a provided vmo with default data in the RAW12 image format.
  void FillRAW12(zx::vmo* vmo);

  size_t vmo_size_;
};
}  // namespace camera

#endif  // SRC_CAMERA_CALIBRATION_IMAGE_WRITER_IMAGE_WRITER_H_
