// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_LIB_IMAGE_WRITER_BLC_H_
#define SRC_CAMERA_LIB_IMAGE_WRITER_BLC_H_

#include <lib/zx/vmo.h>

#include <vector>

namespace camera {

// Struct to pass around the results of the BLC algorithm.
struct BlcResult {
  uint32_t avg_r, avg_gr, avg_gb, avg_b;
};

// Comparator logic for blc_result
inline bool operator==(const BlcResult lhs, const BlcResult rhs) {
  return lhs.avg_r == rhs.avg_r && lhs.avg_gr == rhs.avg_gr && lhs.avg_gb == rhs.avg_gb &&
         lhs.avg_b == rhs.avg_b;
}

// Args:
//  |vmo| Memory object containing the image to run BLC on.
//  |width| The width of the image; needed to calculate image size and stride.
//  |height| The height of the image; needed to calculate image size.
//  |bytes_per_pixel| Integer denoting how many bytes each pixel consists of; needed to calculate
//                    image stride.
// Returns:
//  A struct containing the results of the BLC run on the passed-in image.
BlcResult BlcRaw12(const zx::vmo* vmo, uint32_t width, uint32_t height, uint8_t bytes_per_pixel);

// Helper method to add integers from a tuple to other integers, in their respective order.
// Args:
//  |first_val| The target integer to add the first tuple value to.
//  |second_val| The target integer to add the second tuple value to.
//  |p| The pair containing values to add to the target integers.
void AddValsFromPairToTargetInts(uint32_t* first_val, uint32_t* second_val,
                                 std::pair<uint16_t, uint16_t> p);

}  // namespace camera

#endif  // SRC_CAMERA_LIB_IMAGE_WRITER_BLC_H_
