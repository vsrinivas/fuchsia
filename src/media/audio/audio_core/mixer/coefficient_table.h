// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_MIXER_COEFFICIENT_TABLE_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_MIXER_COEFFICIENT_TABLE_H_

#include <vector>

namespace media::audio::mixer {

// CoefficientTable is a shim around std::vector that maps indicies into a physical addressing
// scheme that is most optimal WRT to how this table is typically accessed. Specifically accesses
// are most commonly with an integral stride (that is 1 << frac_bits stride). Optimize for this use
// case by placing these values physically contiguously in memory.
//
// Note we still expose iterators, but the order is no longer preserved (iteration will be performed
// in physical order).
class CoefficientTable {
 public:
  // |width| is the filter width of this table, in fixed point format with |frac_bits| of fractional
  // precision. The |width| will determine the number of entries in the table, which will be |width|
  // rounded up to the nearest integer in the same fixed-point format.
  CoefficientTable(uint32_t width, uint32_t frac_bits)
      : stride_(ComputeStride(width, frac_bits)),
        frac_bits_(frac_bits),
        frac_mask_((1 << frac_bits_) - 1),
        table_(stride_ * (1 << frac_bits)) {}

  float& operator[](uint32_t offset) { return table_[PhysicalIndex(offset)]; }

  const float& operator[](uint32_t offset) const { return table_[PhysicalIndex(offset)]; }

  auto begin() { return table_.begin(); }
  auto end() { return table_.end(); }

  size_t PhysicalIndex(uint32_t offset) const {
    auto integer = offset >> frac_bits_;
    auto fraction = offset & frac_mask_;
    return fraction * stride_ + integer;
  }

 private:
  static uint32_t ComputeStride(uint32_t filter_width, uint32_t frac_bits) {
    return (filter_width + ((1 << frac_bits) - 1)) / (1 << frac_bits);
  }

  const uint32_t stride_;
  const uint32_t frac_bits_;
  const uint32_t frac_mask_;
  std::vector<float> table_;
};

}  // namespace media::audio::mixer

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_MIXER_COEFFICIENT_TABLE_H_
