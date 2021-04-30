// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_MIXER_FILTER_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_MIXER_FILTER_H_

#include <lib/syslog/cpp/macros.h>

#include <cmath>
#include <memory>
#include <vector>

#include "src/media/audio/audio_core/mixer/coefficient_table.h"
#include "src/media/audio/audio_core/mixer/constants.h"
#include "src/media/audio/lib/format/constants.h"

namespace media::audio::mixer {

// This class represents a symmetric, convolution-based filter, to be applied to an audio stream.
//
// Param side_length is the number of subframes included on each side, including center subframe 0.
// Child classes differ only in their filter coefficients.
class Filter {
 public:
  Filter(int32_t source_rate, int32_t dest_rate, int64_t side_length,
         int32_t num_frac_bits = Fixed::Format::FractionalBits)
      : source_rate_(source_rate),
        dest_rate_(dest_rate),
        side_length_(side_length),
        num_frac_bits_(num_frac_bits),
        frac_size_(1 << num_frac_bits),
        rate_conversion_ratio_(static_cast<double>(dest_rate_) / source_rate_) {
    FX_DCHECK(source_rate_ > 0);
    FX_DCHECK(dest_rate_ > 0);
    FX_DCHECK(side_length > 0);
    FX_DCHECK(num_frac_bits_ > 0);
  }

  virtual float ComputeSample(int64_t frac_offset, float* center) = 0;

  int32_t source_rate() const { return source_rate_; }
  int32_t dest_rate() const { return dest_rate_; }
  int64_t side_length() const { return side_length_; }
  int32_t num_frac_bits() const { return num_frac_bits_; }
  int64_t frac_size() const { return frac_size_; }
  double rate_conversion_ratio() const { return rate_conversion_ratio_; };

  // used for debugging purposes only
  virtual void Display() = 0;

  // Eagerly precompute needed data. If not called, lazily compute on the first ComputeSample() call
  // TODO(fxbug.dev/45074): This is for tests only and can be removed once filter creation is eager.
  virtual void EagerlyPrepare() = 0;

 protected:
  float ComputeSampleFromTable(const CoefficientTable& filter_coefficients, int64_t frac_offset,
                               float* center);
  void DisplayTable(const CoefficientTable& filter_coefficients);

 private:
  int32_t source_rate_;
  int32_t dest_rate_;
  int64_t side_length_;

  int32_t num_frac_bits_;
  int64_t frac_size_;

  double rate_conversion_ratio_;
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
//
// TODO(fxbug.dev/37356): Make the fixed-point fractional scale typesafe.
class PointFilter : public Filter {
 public:
  PointFilter(int32_t source_rate, int32_t dest_rate,
              int32_t num_frac_bits = Fixed::Format::FractionalBits)
      : Filter(source_rate, dest_rate,
               /* side_length= */ (1 << (num_frac_bits - 1)) + 1, num_frac_bits),
        filter_coefficients_(cache_, Inputs{
                                         .side_length = this->side_length(),
                                         .num_frac_bits = this->num_frac_bits(),
                                     }) {}
  PointFilter() : PointFilter(48000, 48000){};

  float ComputeSample(int64_t frac_offset, float* center) override {
    return ComputeSampleFromTable(*filter_coefficients_, frac_offset, center);
  }

  void Display() override { DisplayTable(*filter_coefficients_); }

  float& operator[](size_t index) { return (*filter_coefficients_)[index]; }

  void EagerlyPrepare() override { filter_coefficients_.get(); }

 private:
  struct Inputs {
    int64_t side_length;
    int32_t num_frac_bits;

    bool operator<(const Inputs& rhs) const {
      return std::tie(side_length, num_frac_bits) < std::tie(rhs.side_length, rhs.num_frac_bits);
    }
  };

  friend CoefficientTable* CreatePointFilterTable(Inputs);
  using CacheT = CoefficientTableCache<Inputs>;

  static CacheT* const cache_;
  LazySharedCoefficientTable<Inputs> filter_coefficients_;
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
class LinearFilter : public Filter {
 public:
  LinearFilter(int32_t source_rate, int32_t dest_rate,
               int32_t num_frac_bits = Fixed::Format::FractionalBits)
      : Filter(source_rate, dest_rate,
               /* side_length= */ 1 << num_frac_bits, num_frac_bits),
        filter_coefficients_(cache_, Inputs{
                                         .side_length = this->side_length(),
                                         .num_frac_bits = this->num_frac_bits(),
                                     }) {}
  LinearFilter() : LinearFilter(48000, 48000){};

  float ComputeSample(int64_t frac_offset, float* center) override {
    return ComputeSampleFromTable(*filter_coefficients_, frac_offset, center);
  }

  void Display() override { DisplayTable(*filter_coefficients_); }

  float& operator[](size_t index) { return (*filter_coefficients_)[index]; }

  void EagerlyPrepare() override { filter_coefficients_.get(); }

 private:
  struct Inputs {
    int64_t side_length;
    int32_t num_frac_bits;

    bool operator<(const Inputs& rhs) const {
      return std::tie(side_length, num_frac_bits) < std::tie(rhs.side_length, rhs.num_frac_bits);
    }
  };

  friend CoefficientTable* CreateLinearFilterTable(Inputs);
  using CacheT = CoefficientTableCache<Inputs>;

  static CacheT* const cache_;
  LazySharedCoefficientTable<Inputs> filter_coefficients_;
};

// "Fractional-delay" sinc-based resampler with integrated low-pass filter.
class SincFilter : public Filter {
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

  SincFilter(int32_t source_rate, int32_t dest_rate, int64_t side_length = kFracSideLength,
             int32_t num_frac_bits = Fixed::Format::FractionalBits)
      : Filter(source_rate, dest_rate, side_length, num_frac_bits),
        filter_coefficients_(cache_, Inputs{
                                         .side_length = this->side_length(),
                                         .num_frac_bits = this->num_frac_bits(),
                                         .rate_conversion_ratio = this->rate_conversion_ratio(),
                                     }) {}
  SincFilter() : SincFilter(48000, 48000){};

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

  float ComputeSample(int64_t frac_offset, float* center) override {
    return ComputeSampleFromTable(*filter_coefficients_, frac_offset, center);
  }

  void Display() override { DisplayTable(*filter_coefficients_); }

  float& operator[](size_t index) { return (*filter_coefficients_)[index]; }

  void EagerlyPrepare() override { filter_coefficients_.get(); }

 private:
  struct Inputs {
    int64_t side_length;
    int32_t num_frac_bits;
    double rate_conversion_ratio;

    bool operator<(const Inputs& rhs) const {
      return std::tie(side_length, num_frac_bits, rate_conversion_ratio) <
             std::tie(rhs.side_length, rhs.num_frac_bits, rhs.rate_conversion_ratio);
    }
  };

  using CacheT = CoefficientTableCache<Inputs>;

  friend CacheT* CreateSincFilterCoefficientTableCache();
  friend CoefficientTable* CreateSincFilterTable(Inputs);

  static CacheT* const cache_;
  static std::vector<CacheT::SharedPtr>* persistent_cache_;

  LazySharedCoefficientTable<Inputs> filter_coefficients_;
};

}  // namespace media::audio::mixer

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_MIXER_FILTER_H_
