// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_LIB_IMAGE_UTILS_COLOR_SOURCE_H_
#define SRC_CAMERA_LIB_IMAGE_UTILS_COLOR_SOURCE_H_

#include <stdint.h>
#include <stdio.h>

namespace camera {

// ColorSource steps through hue at a constant rate in HSV colorspace,
// with saturation and value remaining constant. An RGB color is written to
// a buffer provided.
class ColorSource {
 public:
  // Write the next color in the progression to the buffer.
  void FillARGB(void* start, size_t buffer_size);

  static void hsv_color(uint32_t index, uint8_t* r, uint8_t* g, uint8_t* b);

 private:
  static constexpr uint32_t kStartingFrameColor = 0x80;
  uint32_t frame_color_ = kStartingFrameColor;
};

}  // namespace camera

#endif  // SRC_CAMERA_LIB_IMAGE_UTILS_COLOR_SOURCE_H_
