// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef SRC_CAMERA_LIB_RAW_FORMATS_RAW_COLOR_FILTER_H_
#define SRC_CAMERA_LIB_RAW_FORMATS_RAW_COLOR_FILTER_H_

#include "src/camera/lib/raw_formats/pointer_list.h"
#include "src/camera/lib/raw_formats/raw_hash.h"

namespace camera::raw {

/* Pixel colors that can make up the color filter. The 3 tranditional bayer colors are red, green,
   and blue, with green appearing twice. However in some instances it is useful to be able to
   distinguish between the two green pixels, so GREENr (for the greens on the same row as RED
   pixels) and GREENb (for greens on the same as BLUE pixles) have been added.

   There are also camera color filters which have moved away from the bayer patterns/colors and this
   library should be able to support them as well. However, other colors will be added to this
   enumeration as the actual formats which require them are added. This avoids guessing and adding
   things we may never need or that may clash with things we want to add in the future. However some
   of those future colors may include GREEN (for formats with only one green value), YELLOW, WHITE
   and/or CLEAR, EMERALD, ane NIR (near infra-red).
 */
enum class PixelColor {
  RED,
  GREENr,
  GREENb,
  BLUE,
};

/* Represents the color filter of a raw image. Some examples include the 4 basic bayer patterns
   which are defined as constants below. Many (but not all) raw formats will require the image width
   and height to be even multiples of the color filter pattern width and height respectively.
*/
struct ColorFilter {
  const uint32_t width;
  const uint32_t height;
  const PointerList<PixelColor> rows;

  constexpr explicit ColorFilter(const PointerList<PixelColor>& r)
      : width(static_cast<uint32_t>(r.element_array_size())),
        height(static_cast<uint32_t>(r.size())),
        rows(r) {}

  constexpr explicit ColorFilter(PointerList<PixelColor>&& r)
      : width(static_cast<uint32_t>(r.element_array_size())),
        height(static_cast<uint32_t>(r.size())),
        rows(std::move(r)) {}
};

// Given a ColorFilter, return the color of the specified pixel.
// Pixel is specified as an x (width) and y (height) coordinate.
constexpr PixelColor GetPixelColor(const ColorFilter& filter, uint32_t x, uint32_t y) {
  uint32_t filter_y = y % filter.height;
  uint32_t filter_x = x % filter.width;
  return filter.rows[filter_y][filter_x];
}

// Given a ColorFilter and the image width/height, return the color of the specified pixel.
// Pixel is specified as an index (0 to (width * height) - 1).
constexpr PixelColor GetPixelColor(const ColorFilter& filter, uint32_t width, uint32_t /*height*/,
                                   uint32_t pixel_index) {
  uint32_t y = pixel_index / width;
  uint32_t x = pixel_index - (y * width);

  return GetPixelColor(filter, x, y);
}

namespace internal {

template <>
struct hash<ColorFilter> {
  constexpr size_t operator()(ColorFilter const& filter) {
    size_t seed = 0;
    internal::hash_combine(seed, filter.width);
    internal::hash_combine(seed, filter.height);

    for (uint32_t j = 0; j < filter.height; ++j) {
      for (uint32_t i = 0; i < filter.width; ++i) {
        internal::hash_combine(seed, filter.rows[j][i]);
      }
    }
    return seed;
  }
};

constexpr const PixelColor kBGGRRow0[] = {PixelColor::BLUE, PixelColor::GREENb};
constexpr const PixelColor kBGGRRow1[] = {PixelColor::GREENr, PixelColor::RED};
constexpr const PixelColor* kBGGRRows[] = {&(kBGGRRow0[0]), &(kBGGRRow1[0])};
constexpr const uint64_t kBGGRWidth = sizeof(kBGGRRow0) / sizeof(*kBGGRRow0);
constexpr const uint64_t kBGGRHeight = sizeof(kBGGRRows) / sizeof(*kBGGRRows);
constexpr const PointerList<PixelColor> kBGGRRowsList(kBGGRRows, kBGGRHeight, kBGGRWidth);

constexpr const PixelColor kGBRGRow0[] = {PixelColor::GREENb, PixelColor::BLUE};
constexpr const PixelColor kGBRGRow1[] = {PixelColor::RED, PixelColor::GREENr};
constexpr const PixelColor* kGBRGRows[] = {&(kGBRGRow0[0]), &(kGBRGRow1[0])};
constexpr const uint64_t kGBRGWidth = sizeof(kGBRGRow0) / sizeof(*kGBRGRow0);
constexpr const uint64_t kGBRGHeight = sizeof(kGBRGRows) / sizeof(*kGBRGRows);
constexpr const PointerList<PixelColor> kGBRGRowsList(kGBRGRows, kGBRGHeight, kGBRGWidth);

constexpr const PixelColor kGRBGRow0[] = {PixelColor::GREENr, PixelColor::RED};
constexpr const PixelColor kGRBGRow1[] = {PixelColor::BLUE, PixelColor::GREENb};
constexpr const PixelColor* kGRBGRows[] = {&(kGRBGRow0[0]), &(kGRBGRow1[0])};
constexpr const uint64_t kGRBGWidth = sizeof(kGRBGRow0) / sizeof(*kGRBGRow0);
constexpr const uint64_t kGRBGHeight = sizeof(kGRBGRows) / sizeof(*kGRBGRows);
constexpr const PointerList<PixelColor> kGRBGRowsList(kGRBGRows, kGRBGHeight, kGRBGWidth);

constexpr const PixelColor kRGGBRow0[] = {PixelColor::RED, PixelColor::GREENr};
constexpr const PixelColor kRGGBRow1[] = {PixelColor::GREENb, PixelColor::BLUE};
constexpr const PixelColor* kRGGBRows[] = {&(kRGGBRow0[0]), &(kRGGBRow1[0])};
constexpr const uint64_t kRGGBWidth = sizeof(kRGGBRow0) / sizeof(*kRGGBRow0);
constexpr const uint64_t kRGGBHeight = sizeof(kRGGBRows) / sizeof(*kRGGBRows);
constexpr const PointerList<PixelColor> kRGGBRowsList(kRGGBRows, kRGGBHeight, kRGGBWidth);

}  // namespace internal

// The 4 common bayer phases.
constexpr ColorFilter kBayerBGGR(internal::kBGGRRowsList);
constexpr ColorFilter kBayerGBRG(internal::kGBRGRowsList);
constexpr ColorFilter kBayerGRBG(internal::kGRBGRowsList);
constexpr ColorFilter kBayerRGGB(internal::kRGGBRowsList);

}  // namespace camera::raw

#endif  // SRC_CAMERA_LIB_RAW_FORMATS_RAW_COLOR_FILTER_H_
