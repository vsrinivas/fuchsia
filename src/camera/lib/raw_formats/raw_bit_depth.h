// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef SRC_CAMERA_LIB_RAW_FORMATS_RAW_BIT_DEPTH_H_
#define SRC_CAMERA_LIB_RAW_FORMATS_RAW_BIT_DEPTH_H_

#include "src/camera/lib/raw_formats/pointer_list.h"
#include "src/camera/lib/raw_formats/raw_hash.h"

namespace camera::raw {

// This describes the number of bits used for each pixel in a color filter.
struct BitDepthMap {
  const uint32_t width;
  const uint32_t height;
  const PointerList<uint8_t> rows;

  constexpr explicit BitDepthMap(const PointerList<uint8_t>& r)
      : width(static_cast<uint32_t>(r.element_array_size())),
        height(static_cast<uint32_t>(r.size())),
        rows(r) {}

  constexpr explicit BitDepthMap(PointerList<uint8_t>&& r)
      : width(static_cast<uint32_t>(r.element_array_size())),
        height(static_cast<uint32_t>(r.size())),
        rows(std::move(r)) {}
};

// Given a BitDepthMap, return the bit depth of the specified pixel.
// Pixel is specified as an x (width) and y (height) coordinate.
constexpr uint8_t GetBitDepth(const BitDepthMap& map, uint32_t x, uint32_t y) {
  uint32_t map_y = y % map.height;
  uint32_t map_x = x % map.width;
  return map.rows[map_y][map_x];
}

// Given a ColorFilter and the image width/height, return the bit depth of the specified pixel.
// Pixel is specified as an index (0 to (width * height) - 1).
constexpr uint8_t GetBitDepth(const BitDepthMap& map, uint32_t width, uint32_t /*height*/,
                              uint32_t pixel_index) {
  uint32_t y = pixel_index / width;
  uint32_t x = pixel_index - (y * width);

  return GetBitDepth(map, x, y);
}

namespace internal {

template <>
struct hash<BitDepthMap> {
  constexpr size_t operator()(BitDepthMap const& map) {
    size_t seed = 0;
    internal::hash_combine(seed, map.width);
    internal::hash_combine(seed, map.height);

    for (uint32_t j = 0; j < map.height; ++j) {
      for (uint32_t i = 0; i < map.width; ++i) {
        internal::hash_combine(seed, map.rows[j][i]);
      }
    }
    return seed;
  }
};

constexpr const uint8_t kBayer10DepthRow0[] = {10, 10};
constexpr const uint8_t kBayer10DepthRow1[] = {10, 10};
constexpr const uint8_t* kBayer10DepthRows[] = {kBayer10DepthRow0, kBayer10DepthRow1};
constexpr const uint64_t kBayer10Width = sizeof(kBayer10DepthRow0) / sizeof(*kBayer10DepthRow0);
constexpr const uint64_t kBayer10Height = sizeof(kBayer10DepthRows) / sizeof(*kBayer10DepthRows);
constexpr const PointerList<uint8_t> kBayer10DepthList(kBayer10DepthRows, kBayer10Height,
                                                       kBayer10Width);

}  // namespace internal

constexpr const BitDepthMap kBayer10DepthMap(internal::kBayer10DepthList);

}  // namespace camera::raw

#endif  // SRC_CAMERA_LIB_RAW_FORMATS_RAW_BIT_DEPTH_H_
