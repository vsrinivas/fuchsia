// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef SRC_CAMERA_LIB_RAW_FORMATS_RAW_H_
#define SRC_CAMERA_LIB_RAW_FORMATS_RAW_H_

#include "src/camera/lib/raw_formats/raw_bit_depth.h"
#include "src/camera/lib/raw_formats/raw_color_filter.h"
#include "src/camera/lib/raw_formats/raw_packing.h"

namespace camera::raw {

// Describes a RAW bayer format.
struct RawFormat {
  // A numeric ID for the format, calculated by hashing the contents.
  const uint64_t id;
  // Describes how the pixels of this format are packed.
  const PackingBlock packing_block;
  // Describes the color content of the image pixels.
  const ColorFilter color_filter;
  // Describes the bit depth of the image pixels.
  const BitDepthMap depth_map;

  constexpr RawFormat(const PackingBlock& block, const ColorFilter& filter, const BitDepthMap& map)
      : id(hash(block, filter, map)), packing_block(block), color_filter(filter), depth_map(map) {}

  template <typename PbT, typename CfT, typename BmT>
  RawFormat(PbT&& block, CfT&& filter, BmT&& map)
      : id(hash(block, filter, map)),
        packing_block(std::forward<PbT>(block)),
        color_filter(std::forward<CfT>(filter)),
        depth_map(std::forward<BmT>(map)) {}

  constexpr bool operator==(const RawFormat& o) const { return id == o.id; }

 private:
  static constexpr size_t hash(const PackingBlock& block, const ColorFilter& filter,
                               const BitDepthMap& map) {
    size_t seed = 0;
    internal::hash_combine(seed, block);
    internal::hash_combine(seed, filter);
    internal::hash_combine(seed, map);
    return seed;
  }
};

// Describes a RAW bayer format of a particular width, height, and stride (if applicable). The
// packing block (and everything it contains) will have entirely FINITE repeat values. This
// data structure can be used to do look-ups of individual pixels within a buffer.
struct RawFormatInstance {
  const uint64_t id;
  const uint64_t raw_id;
  const uint32_t width;
  const uint32_t height;
  const std::optional<uint32_t> row_stride;
  const size_t size;
  const PackingBlock packing_block;
  const ColorFilter color_filter;
  const BitDepthMap depth_map;

  constexpr bool operator==(const RawFormatInstance& o) const { return id == o.id; }

 private:
  template <typename PbT, typename CfT, typename BmT>
  RawFormatInstance(uint64_t rid, uint32_t w, uint32_t h, std::optional<int32_t> rs, PbT&& pb,
                    CfT&& filter, BmT&& map)
      : id(hash(rid, w, h, rs)),
        raw_id(rid),
        width(w),
        height(h),
        row_stride(rs),
        size(pb.repeat().times * pb.size()),
        packing_block(std::forward<PbT>(pb)),
        color_filter(std::forward<CfT>(filter)),
        depth_map(std::forward<BmT>(map)) {}

  static constexpr size_t hash(uint64_t rid, uint32_t width, uint32_t height,
                               const std::optional<uint32_t> row_stride) {
    size_t seed = 0;
    internal::hash_combine(seed, rid);
    internal::hash_combine(seed, width);
    internal::hash_combine(seed, height);
    if (row_stride) {
      internal::hash_combine(seed, *row_stride);
    }
    return seed;
  }

  friend RawFormatInstance CreateFormatInstance(const RawFormat& format, uint32_t width,
                                                uint32_t height,
                                                std::optional<uint32_t> row_stride);
};

// Create an instance of a RawFormat given a specified width, height and row stride (if applicable).
RawFormatInstance CreateFormatInstance(const RawFormat& format, uint32_t width, uint32_t height,
                                       std::optional<uint32_t> row_stride = std::nullopt);

}  // namespace camera::raw

#endif  // SRC_CAMERA_LIB_RAW_FORMATS_RAW_H_
