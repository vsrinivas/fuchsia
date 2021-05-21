// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_MIXER_COEFFICIENT_TABLE_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_MIXER_COEFFICIENT_TABLE_H_

#include <lib/stdcompat/span.h>

#include <cmath>
#include <map>
#include <memory>
#include <mutex>
#include <vector>

#include "src/media/audio/audio_core/mixer/constants.h"
#include "src/media/audio/lib/format/constants.h"

// coefficient_table.h is included by gen_coefficient_tables.cc, which does not have access to
// Fuchsia headers because it is compiled as a host binary.
#ifndef BUILDING_FUCHSIA_AUDIO_HOST_TOOL
#include <lib/syslog/cpp/macros.h>  // nogncheck
#else
#include <cassert>
#define FX_CHECK(cond) assert(cond)
#endif

namespace media::audio::mixer {

// CoefficientTable is a shim around std::vector that maps indicies into a physical addressing
// scheme that is most optimal WRT to how this table is typically accessed. Specifically accesses
// are most commonly with an integral stride (that is 1 << frac_bits stride). Optimize for this use
// case by placing these values physically contiguously in memory.
//
// Coefficient tables represent one side of a symmetric convolution filter. Coefficients cover the
// entire discrete space of fractional position values, but for any calculation we reference only
// a small subset of these values (see ReadSlice for an example).
class CoefficientTable {
 public:
  // |width| is the filter width of this table, in fixed point format with |frac_bits| of fractional
  // precision. The |width| will determine the number of entries in the table, which will be |width|
  // rounded up to the nearest integer in the same fixed-point format. |data| provides the raw table
  // data ordered by physical address. If |data| is empty, storage is allocated automatically.
  CoefficientTable(int64_t width, int32_t frac_bits, cpp20::span<const float> data)
      : stride_(ComputeStride(width, frac_bits)),
        frac_filter_width_(width),
        frac_bits_(frac_bits),
        frac_mask_((1 << frac_bits_) - 1),
        storage_(data.empty() ? std::make_optional<std::vector<float>>(stride_ * (1 << frac_bits))
                              : std::nullopt),
        table_(data.empty() ? cpp20::span<const float>(&(*storage_)[0], storage_->size()) : data) {
    FX_CHECK(frac_filter_width_ >= 0);
    FX_CHECK(static_cast<int64_t>(table_.size()) == stride_ * (1 << frac_bits));
  }

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

  // Returns the raw table in physical (not logical) order.
  cpp20::span<const float> raw_table() const { return table_; }

  // Returns the physical index corresponding to the given logical index.
  size_t PhysicalIndex(int64_t offset) const {
    auto integer = offset >> frac_bits_;
    auto fraction = offset & frac_mask_;
    return fraction * stride_ + integer;
  }

 private:
  friend class CoefficientTableBuilder;
  friend class CoefficientTableTest;

  static int64_t ComputeStride(int64_t filter_width, int32_t frac_bits) {
    return (filter_width + ((1 << frac_bits) - 1)) / (1 << frac_bits);
  }

  const int64_t stride_;
  const int64_t frac_filter_width_;
  const int32_t frac_bits_;
  const int64_t frac_mask_;

  // The table_ can reference the storage vector storage_ or an externally-allocated array,
  // such as an array allocated in .rodata.
  std::optional<std::vector<float>> storage_;
  cpp20::span<const float> table_;
};

// CoefficientTableBuilder constructs a single CoefficientTable.
// Once constructed, the CoefficientTable is immutable.
class CoefficientTableBuilder {
 public:
  CoefficientTableBuilder(int64_t width, int32_t frac_bits)
      : table_(std::make_unique<CoefficientTable>(width, frac_bits, cpp20::span<const float>{})) {}

  float& operator[](int64_t offset) { return (*table_->storage_)[table_->PhysicalIndex(offset)]; }

  auto physical_index_begin() { return table_->storage_->begin(); }
  auto physical_index_end() { return table_->storage_->end(); }
  size_t size() const { return table_->storage_->size(); }

  std::unique_ptr<CoefficientTable> Build() { return std::move(table_); }

 private:
  std::unique_ptr<CoefficientTable> table_;
};

// Nearest-neighbor "zero-order interpolation" resampler, implemented using the convolution
// filter. Length on both sides is half a frame + 1 subframe (expressed in our fixed-point
// fractional scale), modulo the stretching effects of downsampling.
//
// Example: for frac_size 1000, filter_length would be 500, entailing coefficient values for
// locations from that exact position, up to positions as much as 500 away. This means:
// - Fractional source pos 1.499 requires frames between 0.999 and 1.999, thus source frame 1
// - Fractional source pos 1.500 requires frames between 1.000 and 2.000, thus source frames 1 and 2
// - Fractional source pos 1.501 requires frames between 1.001 and 2.001, thus source frame 2
// For source pos .5, we average the pre- and post- values so as to achieve zero phase delay
class PointFilterCoefficientTable {
 public:
  struct Inputs {
    int64_t side_length;
    int32_t num_frac_bits;

    bool operator<(const Inputs& rhs) const {
      return std::tie(side_length, num_frac_bits) < std::tie(rhs.side_length, rhs.num_frac_bits);
    }
  };

  static std::unique_ptr<CoefficientTable> Create(Inputs);
};

// Linear interpolation, implemented using the convolution filter.
// Length on both sides is one frame, modulo the stretching effects of downsampling.
//
// Example: for frac_size 1000, filter_length would be 999, entailing coefficient values for
// locations from that exact position, up to positions as much as 999 away. This means:
// -Fractional source pos 1.999 requires frames between 1.000 and 2.998, thus source frames 1 and 2
// -Fractional source pos 2.001 requires frames between 1.002 and 3.000, thus source frames 2 and 3
// -Fractional source pos 2.000 requires frames between 1.001 and 2.999, thus source frame 2 only
//  (Restated: source pos N.000 requires frame N only; no need to interpolate with neighbors.)
class LinearFilterCoefficientTable {
 public:
  struct Inputs {
    int64_t side_length;
    int32_t num_frac_bits;

    bool operator<(const Inputs& rhs) const {
      return std::tie(side_length, num_frac_bits) < std::tie(rhs.side_length, rhs.num_frac_bits);
    }
  };

  static std::unique_ptr<CoefficientTable> Create(Inputs);
};

// "Fractional-delay" sinc-based resampler with integrated low-pass filter.
class SincFilterCoefficientTable {
 public:
  static constexpr int32_t kSideTaps = 13;
  static constexpr int64_t kFracSideLength = (kSideTaps + 1) << Fixed::Format::FractionalBits;

  // 27.5:1 allows 192 KHz to be downsampled to 6980 Hz with all taps engaged (i.e. at full
  // quality). It also allows 192:1 downsampling filters to have at least 2 tap lengths of quality.
  static constexpr double kMaxDownsampleRatioForFullSideTaps = 27.5;
  static constexpr int64_t kMaxFracSideLength = static_cast<int64_t>(
      kMaxDownsampleRatioForFullSideTaps * static_cast<double>(kFracSideLength));
  static_assert(kMaxFracSideLength > kFracSideLength,
                "kMaxFracSideLength cannot be less than kFracSideLength");

  struct Inputs {
    int64_t side_length;
    int32_t num_frac_bits;
    double rate_conversion_ratio;

    bool operator<(const Inputs& rhs) const {
      return std::tie(side_length, num_frac_bits, rate_conversion_ratio) <
             std::tie(rhs.side_length, rhs.num_frac_bits, rhs.rate_conversion_ratio);
    }
  };

  static inline Fixed Length(int32_t source_frame_rate, int32_t dest_frame_rate) {
    int64_t filter_length = kFracSideLength;
    if (source_frame_rate > dest_frame_rate) {
      filter_length =
          static_cast<int64_t>(std::ceil(static_cast<double>(filter_length * source_frame_rate) /
                                         static_cast<double>(dest_frame_rate)));

      // For down-sampling ratios beyond kMaxDownsampleRatioForFullSideTaps the effective number of
      // side taps decreases proportionally -- rate-conversion quality gracefully degrades.
      filter_length = std::min(filter_length, kMaxFracSideLength);
    }

    return Fixed::FromRaw(filter_length);
  }

  static Inputs MakeInputs(int32_t source_rate, int32_t dest_rate) {
    return Inputs{
        .side_length = Length(source_rate, dest_rate).raw_value(),
        .num_frac_bits = kPtsFractionalBits,
        .rate_conversion_ratio = static_cast<double>(dest_rate) / static_cast<double>(source_rate),
    };
  }

  static std::unique_ptr<CoefficientTable> Create(Inputs);
};

// These global structs This global struct describes a set of prebuilt coefficient tables.
struct PrebuiltSincFilterCoefficientTable {
  int32_t source_rate;
  int32_t dest_rate;
  cpp20::span<const float> table;
};

// The list of prebuilt coefficient tables.
// This uses std::array so it can directly reference data in .rodata without reallocating
// (std::vector would allocate and copy from .rodata).
extern const cpp20::span<const PrebuiltSincFilterCoefficientTable>
    kPrebuiltSincFilterCoefficientTables;

}  // namespace media::audio::mixer

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_MIXER_COEFFICIENT_TABLE_H_
