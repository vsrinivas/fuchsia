// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_MIXER_COEFFICIENT_TABLE_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_MIXER_COEFFICIENT_TABLE_H_

#include <lib/fit/function.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/compiler.h>

#include <map>
#include <memory>
#include <mutex>
#include <vector>

#include "src/lib/fxl/synchronization/thread_annotations.h"

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
  CoefficientTable(int64_t width, int32_t frac_bits)
      : stride_(ComputeStride(width, frac_bits)),
        frac_filter_width_(width),
        frac_bits_(frac_bits),
        frac_mask_((1 << frac_bits_) - 1),
        table_(stride_ * (1 << frac_bits)) {
    FX_DCHECK(frac_filter_width_ >= 0) << "CoefficientTable width cannot be negative";
  }

  float& operator[](int64_t offset) { return table_[PhysicalIndex(offset)]; }
  const float& operator[](int64_t offset) const { return table_[PhysicalIndex(offset)]; }

  // Reads |num_coefficients| coefficients starting at |offset|. The result is a pointer to
  // |num_coefficients| coefficients with the following semantics:
  //
  // auto c = new CoefficientTable(width, frac_bits);
  // auto f = c->ReadSlice(offset, size);
  // ASSERT_EQ(f[0], c[off + 0 << frac_bits]);
  // ASSERT_EQ(f[1], c[off + 1 << frac_bits]);
  //  ...
  // ASSERT_EQ(f[size], c[off + size << frac_bits]);
  const float* ReadSlice(int64_t offset, int64_t num_coefficients) const {
    if (num_coefficients <= 0 ||
        offset + ((num_coefficients - 1) << frac_bits_) > frac_filter_width_) {
      return nullptr;
    }

    // The underlying table already stores these consecutively.
    return &table_[PhysicalIndex(offset)];
  }

  auto begin() { return table_.begin(); }
  auto end() { return table_.end(); }

  size_t PhysicalIndex(int64_t offset) const {
    auto integer = offset >> frac_bits_;
    auto fraction = offset & frac_mask_;
    return fraction * stride_ + integer;
  }

 private:
  static int64_t ComputeStride(int64_t filter_width, int32_t frac_bits) {
    return (filter_width + ((1 << frac_bits) - 1)) / (1 << frac_bits);
  }

  const int64_t stride_;
  const int64_t frac_filter_width_;
  const int32_t frac_bits_;
  const int64_t frac_mask_;
  std::vector<float> table_;
};

}  // namespace media::audio::mixer

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_MIXER_COEFFICIENT_TABLE_H_
