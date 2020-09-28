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

namespace media::audio::mixer {

static constexpr uint32_t kSincFilterSideTaps = 13;
static constexpr uint32_t kSincFilterSideLength = (kSincFilterSideTaps + 1) << kPtsFractionalBits;

// This class represents a convolution-based filter, to be applied to an audio stream. Subclasses
// represent specific filters for nearest-neighbor interpolation, linear interpolation, and
// sinc-based band-pass. Note that each child class owns creating and populating its own
// filter_coefficients vector. More on these details below.
class Filter {
 public:
  Filter(uint32_t source_rate, uint32_t dest_rate, uint32_t side_width = kSincFilterSideLength,
         uint32_t num_frac_bits = kPtsFractionalBits)
      : source_rate_(source_rate),
        dest_rate_(dest_rate),
        side_width_(side_width),
        num_frac_bits_(num_frac_bits),
        frac_size_(1u << num_frac_bits),
        rate_conversion_ratio_(static_cast<double>(dest_rate_) / source_rate_) {
    FX_DCHECK(source_rate_ > 0);
    FX_DCHECK(dest_rate_ > 0);
    FX_DCHECK(side_width > 0);
    FX_DCHECK(num_frac_bits_ > 0);
  }

  virtual float ComputeSample(uint32_t frac_offset, float* center) = 0;

  // used for debugging purposes only
  virtual void Display() = 0;

  void DisplayTable(const CoefficientTable& filter_coefficients);
  float ComputeSampleFromTable(const CoefficientTable& filter_coefficients, uint32_t frac_offset,
                               float* center);

  uint32_t source_rate() const { return source_rate_; }
  uint32_t dest_rate() const { return dest_rate_; }
  uint32_t side_width() const { return side_width_; }
  uint32_t num_frac_bits() const { return num_frac_bits_; }
  uint32_t frac_size() const { return frac_size_; }
  double rate_conversion_ratio() const { return rate_conversion_ratio_; };

  // Eagerly precompute any needed data. If not called, that data should be lazily computed
  // on the first call to ComputeSample().
  // TODO(fxbug.dev/45074): This is for tests only and can be removed once filter creation is eager.
  virtual void EagerlyPrepare() = 0;

 private:
  uint32_t source_rate_;
  uint32_t dest_rate_;
  uint32_t side_width_;

  uint32_t num_frac_bits_;
  uint32_t frac_size_;

  double rate_conversion_ratio_;
};

// These child classes differ only in their filter coefficients. As mentioned above, each child
// class owns its own filter_coefficients vector, which represents one side of the filter (these
// classes expect the convolution filter to be symmetric). Also, filter coefficients cover the
// entire discrete space of fractional position values, but for any calculation we reference only
// a small subset of these values (using a stride size of one source frame: frac_size_).
//
// Nearest-neighbor "zero-order interpolation" resampler, implemented using the convolution
// filter. Width on both sides is FRAC_HALF (expressed in our fixed-point fractional scale),
// modulo the stretching effects of downsampling.
//
// Why do we say Point Interpolation's filter width is "FRAC_HALF", even as we send FRAC_HALF+1?
// Let's pretend that frac_size is 1000. Filter_width 501 includes coefficient values for
// locations from that exact position, up to positions as much as 500 away. This means:
// -Fractional source pos 1.499 requires frames between 0.999 and 1.999, thus source frame 1
// -Fractional source pos 1.500 requires frames between 1.000 and 2.000, thus source frames 1 and 2
// -Fractional source pos 1.501 requires frames between 1.001 and 2.001, thus source frame 2
// For frac src pos .5, we average the pre- and post- values so as to achieve zero phase delay
//
// TODO(fxbug.dev/37356): Make the fixed-point fractional scale typesafe.
class PointFilter : public Filter {
 public:
  PointFilter(uint32_t source_rate, uint32_t dest_rate, uint32_t num_frac_bits = kPtsFractionalBits)
      : Filter(source_rate, dest_rate,
               /* side_width= */ (1u << (num_frac_bits - 1u)) + 1u, num_frac_bits),
        filter_coefficients_(cache_, Inputs{
                                         .side_width = this->side_width(),
                                         .num_frac_bits = this->num_frac_bits(),
                                     }) {}
  PointFilter() : PointFilter(48000, 48000){};

  float ComputeSample(uint32_t frac_offset, float* center) override {
    return ComputeSampleFromTable(*filter_coefficients_, frac_offset, center);
  }

  void Display() override { DisplayTable(*filter_coefficients_); }

  float& operator[](size_t index) { return (*filter_coefficients_)[index]; }

  void EagerlyPrepare() override { filter_coefficients_.get(); }

 private:
  struct Inputs {
    uint32_t side_width;
    uint32_t num_frac_bits;

    bool operator<(const Inputs& rhs) const {
      return std::tie(side_width, num_frac_bits) < std::tie(rhs.side_width, rhs.num_frac_bits);
    }
  };

  friend CoefficientTable* CreatePointFilterTable(Inputs);
  using CacheT = CoefficientTableCache<Inputs>;

  static CacheT* const cache_;
  LazySharedCoefficientTable<Inputs> filter_coefficients_;
};

// Linear interpolation, implemented using the convolution filter.
// Width on both sides is FRAC_ONE-1, modulo the stretching effects of downsampling.
//
// Why do we say Linear Interpolation's filter width is "FRAC_ONE-1", although we send FRAC_ONE?
// Let's pretend that frac_size is 1000. Filter_width 1000 includes coefficient values for
// locations from that exact position, up to positions as much as 999 away. This means:
// -Fractional source pos 1.999 requires frames between 1.000 and 2.998, thus source frames 1 and 2
// -Fractional source pos 2.000 requires frames between 1.001 and 2.999, thus source frame 2 only
// -Fractional source pos 2.001 requires frames between 1.002 and 3.000, thus source frames 2 and 3
// For frac src pos .0, we use that value precisely; no need to interpolate with any neighbor
class LinearFilter : public Filter {
 public:
  LinearFilter(uint32_t source_rate, uint32_t dest_rate,
               uint32_t num_frac_bits = kPtsFractionalBits)
      : Filter(source_rate, dest_rate,
               /* side_width= */ 1u << num_frac_bits, num_frac_bits),
        filter_coefficients_(cache_, Inputs{
                                         .side_width = this->side_width(),
                                         .num_frac_bits = this->num_frac_bits(),
                                     }) {}
  LinearFilter() : LinearFilter(48000, 48000){};

  float ComputeSample(uint32_t frac_offset, float* center) override {
    return ComputeSampleFromTable(*filter_coefficients_, frac_offset, center);
  }

  void Display() override { DisplayTable(*filter_coefficients_); }

  float& operator[](size_t index) { return (*filter_coefficients_)[index]; }

  void EagerlyPrepare() override { filter_coefficients_.get(); }

 private:
  struct Inputs {
    uint32_t side_width;
    uint32_t num_frac_bits;

    bool operator<(const Inputs& rhs) const {
      return std::tie(side_width, num_frac_bits) < std::tie(rhs.side_width, rhs.num_frac_bits);
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
  SincFilter(uint32_t source_rate, uint32_t dest_rate, uint32_t side_width = kSincFilterSideLength,
             uint32_t num_frac_bits = kPtsFractionalBits)
      : Filter(source_rate, dest_rate, side_width, num_frac_bits),
        filter_coefficients_(cache_, Inputs{
                                         .side_width = this->side_width(),
                                         .num_frac_bits = this->num_frac_bits(),
                                         .rate_conversion_ratio = this->rate_conversion_ratio(),
                                     }) {}
  SincFilter() : SincFilter(48000, 48000){};

  static inline uint32_t GetFilterWidth(uint32_t source_frame_rate, uint32_t dest_frame_rate) {
    return ((source_frame_rate > dest_frame_rate)
                ? std::ceil((static_cast<double>(kSincFilterSideLength) * source_frame_rate) /
                            dest_frame_rate)
                : kSincFilterSideLength) -
           1u;
  }

  float ComputeSample(uint32_t frac_offset, float* center) override {
    return ComputeSampleFromTable(*filter_coefficients_, frac_offset, center);
  }

  void Display() override { DisplayTable(*filter_coefficients_); }

  float& operator[](size_t index) { return (*filter_coefficients_)[index]; }

  void EagerlyPrepare() override { filter_coefficients_.get(); }

 private:
  struct Inputs {
    uint32_t side_width;
    uint32_t num_frac_bits;
    double rate_conversion_ratio;

    bool operator<(const Inputs& rhs) const {
      return std::tie(side_width, num_frac_bits, rate_conversion_ratio) <
             std::tie(rhs.side_width, rhs.num_frac_bits, rhs.rate_conversion_ratio);
    }
  };

  friend CoefficientTable* CreateSincFilterTable(Inputs);
  using CacheT = CoefficientTableCache<Inputs>;

  static CacheT* const cache_;
  LazySharedCoefficientTable<Inputs> filter_coefficients_;
};

}  // namespace media::audio::mixer

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_MIXER_FILTER_H_
