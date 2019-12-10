// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_IMAGE_WRITER_IMAGE_WRITER_H_
#define SRC_CAMERA_IMAGE_WRITER_IMAGE_WRITER_H_

#include <lib/fzl/vmo-pool.h>

#include "src/camera/drivers/isp/modules/dma-format.h"

namespace camera {

inline constexpr uint16_t kHighByteMask = 0xFF0;
inline constexpr uint16_t kLowHalfByteMask = 0x00F;
inline constexpr uint8_t kDoubleByteShift = 16;
inline constexpr uint8_t kByteShift = 8;
inline constexpr uint8_t kHalfByteShift = 4;
inline constexpr auto kPixelTypeRaw12 = DmaFormat::RAW12;

class ImageWriter {
 public:
  ImageWriter(DmaFormat dma_format, size_t vmo_size)
      : dma_format_(dma_format), vmo_size_(vmo_size) {}
  virtual ~ImageWriter() = default;

  // Virtual method to be implemented by derived classes specific to each supported image format.
  // Creates a vmo of appropriate size (depending on DmaFormat image size) and fills it according to
  // the format corresponding to the derived class.
  // Args:
  //  |vmo| Memory object handle to which the image will be written. This function will create a new
  //        vmo.
  //  |r| Maximum red pixel value.
  //  |g| Maximum green pixel value.
  //  |b| Maximum blue pixel value.
  // Returns:
  //  Whether vmo creation succeeded.
  virtual zx_status_t Write(zx::vmo* vmo, uint16_t r, uint16_t g, uint16_t b) = 0;

  // Getter methods
  DmaFormat DmaFormat() const { return dma_format_; }
  size_t VmoSize() const { return vmo_size_; }

 private:
  const class DmaFormat dma_format_;
  const size_t vmo_size_;
};

}  // namespace camera

#endif  // SRC_CAMERA_IMAGE_WRITER_IMAGE_WRITER_H_
