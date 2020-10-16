// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_LIB_IMAGE_UTILS_IMAGE_WRITER_H_
#define SRC_CAMERA_LIB_IMAGE_UTILS_IMAGE_WRITER_H_

#include <lib/fzl/vmo-pool.h>

namespace camera {

inline constexpr uint16_t kHighByteMask = 0xFF0;
inline constexpr uint16_t kLowHalfByteMask = 0x00F;
inline constexpr uint8_t kDoubleByteShift = 16;
inline constexpr uint8_t kByteShift = 8;
inline constexpr uint8_t kHalfByteShift = 4;

class ImageWriter {
 public:
  ImageWriter(uint32_t width, uint32_t height, size_t vmo_size)
      : width_(width), height_(height), vmo_size_(vmo_size) {}
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
  uint32_t width() const { return width_; }
  uint32_t height() const { return height_; }
  size_t vmo_size() const { return vmo_size_; }

 private:
  const uint32_t width_, height_;
  const size_t vmo_size_;
};

}  // namespace camera

#endif  // SRC_CAMERA_LIB_IMAGE_UTILS_IMAGE_WRITER_H_
