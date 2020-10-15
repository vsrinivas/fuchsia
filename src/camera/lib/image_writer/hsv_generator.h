// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_LIB_IMAGE_WRITER_HSV_GENERATOR_H_
#define SRC_CAMERA_LIB_IMAGE_WRITER_HSV_GENERATOR_H_

#include <fuchsia/sysmem/cpp/fidl.h>
#include <stdint.h>
#include <stdio.h>

namespace camera {

// HsvGenerator steps through hue at a constant rate in HSV colorspace,
// with saturation and value remaining constant. A color is written to
// a buffer provided, in the image format specified.
// Currently only RGB types are supported
zx_status_t HsvGenerator(void* start, const fuchsia::sysmem::ImageFormat_2& format,
                         uint32_t frame_index);

// Converts a packed RGB(A) format into 4 individual values.
// For formats that have fewer than 8 bits per color, the color values are shifted
// to their most significant bits.
zx_status_t RgbaUnpack(fuchsia::sysmem::PixelFormatType format, uint8_t* r, uint8_t* g, uint8_t* b,
                       uint8_t* a, uint32_t packed);

// Converts an r, g, b and a value into an RGB(A) format.
zx_status_t RgbaPack(fuchsia::sysmem::PixelFormatType format, uint8_t r, uint8_t g, uint8_t b,
                     uint8_t a, uint32_t* out);
}  // namespace camera

#endif  // SRC_CAMERA_LIB_IMAGE_WRITER_HSV_GENERATOR_H_
