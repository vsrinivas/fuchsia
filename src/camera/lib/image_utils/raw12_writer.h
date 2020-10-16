// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_LIB_IMAGE_UTILS_RAW12_WRITER_H_
#define SRC_CAMERA_LIB_IMAGE_UTILS_RAW12_WRITER_H_

#include <lib/fit/result.h>

#include "src/camera/lib/image_utils/image_writer.h"

namespace camera {

inline constexpr uint16_t kMaxVal = 4095;
inline constexpr uint16_t kRedPixel = 2048;
inline constexpr uint32_t kBytesPerDoublePixel = 3;

// Helper method that organizes two individual pixel values into the appropriate RAW12
// double-pixel format.
// Args:
//  |first_pixel| A 12-bit value representing either a R or G pixel.
//  |second_pixel| A 12-bit value representing either a G or B pixel.
// Returns:
//  An array of the bytes that were passed in arranged in RAW12 pixel format.
std::array<uint8_t, kBytesPerDoublePixel> PixelValuesToDoublePixel(uint16_t first_pixel_val,
                                                                   uint16_t second_pixel_val);

// Helper method that extracts the original pixel values from a RAW12 double-pixel.
// Args:
//  |double_pixel| An array of bytes arranged in RAW12 pixel format (represents two pixels).
// Returns:
//  A pair of each of the two 12-bit pixel values that comprised the original double-pixel.
std::pair<uint16_t, uint16_t> DoublePixelToPixelValues(
    std::array<uint8_t, kBytesPerDoublePixel> double_pixel);

class Raw12Writer : public ImageWriter {
 public:
  // Constructor.
  // Args:
  //  |width| The width of the image to be written.
  //  |height| The height of the image to be written.
  //  |vmo_size| Number of bytes comprising the intended images to be written (width * height * # of
  //             bytes per pixel).
  explicit Raw12Writer(uint32_t width, uint32_t height, size_t vmo_size)
      : ImageWriter(width, height, vmo_size) {}

  // Factory method that outputs a Raw12Writer with all its params set for the format.
  // Args:
  //  |width| Intended width for images to be written (in # of pixels).
  //  |height| Intended height for images to be written (in # of pixels).
  //  |pixel_format| Intended byte representation for each pixel in the images to be written.
  // Returns:
  //  A Raw12Writer object that writes images to vmos.
  static fit::result<std::unique_ptr<Raw12Writer>, zx_status_t> Create(uint32_t width,
                                                                       uint32_t height);

  // Fills a provided vmo with default data in the RAW12 image format. Only a RGGB layout is used at
  // the moment.
  // The red pixel value stays consistent at a value of r throughout the image.
  // The green pixels value increases from 0 to a maximum of g across individual rows.
  // The blue pixel value increases from 0 to a maximum of b across individual columns.
  //
  // Each double-pixel is laid out like so:
  //  Bits  ->  7        6        5        4        3        2        1        0
  //  Byte0 ->  P0[11]   P0[10]   P0[9]    P0[8]    P0[7]    P0[6]    P0[5]    P0[4]
  //  Byte1 ->  P1[11]   P1[10]   P1[9]    P1[8]    P1[7]    P1[6]    P1[5]    P1[4]
  //  Byte2 ->  P1[3]    P1[2]    P1[1]    P1[0]    P0[3]    P0[2]    P0[1]    P0[0]
  //
  // A RG row with two double-pixels (six bytes) looks like:
  //  R0[11:4], G0[11:4], G0[3:0]R0[3:0], R1[11:4], G1[11:4], G1[3:0]R1[3:0]
  // A GB row with two double pixels (six bytes) looks like:
  //  G0[11:4], B0[11:4], B0[3:0]G0[3:0], G1[11:4], B1[11:4], B1[3:0]G1[3:0]
  //
  // Args:
  //  |vmo| Memory object handle to which the image will be written. The vmo must have been created
  //        before passing it in.
  //  |r| Red pixel value; default is set to kRedPixel.
  //  |g| Green pixel values in the image will range from 0 to g; default is set to kMaxPixelVal.
  //  |b| Blue pixel values in the image will range from 0 to b; default is set to kMaxPixelVal.
  //
  // TODO(nzo): split g into gr and gb.
  // TODO(nzo): use a more straightforward filling method; scaling with step functions causes a few
  //            issues downstream of image creation.
  zx_status_t Write(zx::vmo* vmo, uint16_t r, uint16_t g, uint16_t b) override;
};

}  // namespace camera

#endif  // SRC_CAMERA_LIB_IMAGE_UTILS_RAW12_WRITER_H_
