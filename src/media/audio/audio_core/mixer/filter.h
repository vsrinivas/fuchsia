// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_MIXER_FILTER_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_MIXER_FILTER_H_

#include <lib/syslog/cpp/macros.h>

#include <cmath>
#include <memory>
#include <vector>

#include "src/media/audio/audio_core/mixer/coefficient_table.h"
#include "src/media/audio/audio_core/mixer/coefficient_table_cache.h"
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
  double rate_conversion_ratio() const { return rate_conversion_ratio_; }

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

// See PointFilterCoefficientTable.
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

  float ComputeSample(int64_t frac_offset, float* center) override {
    return ComputeSampleFromTable(*filter_coefficients_, frac_offset, center);
  }

  void Display() override { DisplayTable(*filter_coefficients_); }

  const float& operator[](int64_t index) { return (*filter_coefficients_)[index]; }

  void EagerlyPrepare() override { filter_coefficients_.get(); }

 private:
  using Inputs = PointFilterCoefficientTable::Inputs;
  using CacheT = CoefficientTableCache<Inputs>;

  static CacheT* const cache_;
  LazySharedCoefficientTable<Inputs> filter_coefficients_;
};

// See LinearFilterCoefficientTable.
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

  float ComputeSample(int64_t frac_offset, float* center) override {
    return ComputeSampleFromTable(*filter_coefficients_, frac_offset, center);
  }

  void Display() override { DisplayTable(*filter_coefficients_); }

  const float& operator[](int64_t index) { return (*filter_coefficients_)[index]; }

  void EagerlyPrepare() override { filter_coefficients_.get(); }

 private:
  using Inputs = LinearFilterCoefficientTable::Inputs;
  using CacheT = CoefficientTableCache<Inputs>;

  static CacheT* const cache_;
  LazySharedCoefficientTable<Inputs> filter_coefficients_;
};

// See SincFilterCoefficientTable.
class SincFilter : public Filter {
 public:
  static constexpr auto kSideTaps = SincFilterCoefficientTable::kSideTaps;
  static constexpr auto kFracSideLength = SincFilterCoefficientTable::kFracSideLength;
  static constexpr auto kMaxFracSideLength = SincFilterCoefficientTable::kMaxFracSideLength;

  SincFilter(int32_t source_rate, int32_t dest_rate,
             int64_t side_length = SincFilterCoefficientTable::kFracSideLength,
             int32_t num_frac_bits = Fixed::Format::FractionalBits)
      : Filter(source_rate, dest_rate, side_length, num_frac_bits),
        filter_coefficients_(cache_, Inputs{
                                         .side_length = this->side_length(),
                                         .num_frac_bits = this->num_frac_bits(),
                                         .rate_conversion_ratio = this->rate_conversion_ratio(),
                                     }) {}

  static inline Fixed Length(int32_t source_frame_rate, int32_t dest_frame_rate) {
    return SincFilterCoefficientTable::Length(source_frame_rate, dest_frame_rate);
  }

  float ComputeSample(int64_t frac_offset, float* center) override {
    return ComputeSampleFromTable(*filter_coefficients_, frac_offset, center);
  }

  void Display() override { DisplayTable(*filter_coefficients_); }

  const float& operator[](int64_t index) { return (*filter_coefficients_)[index]; }

  void EagerlyPrepare() override { filter_coefficients_.get(); }

 private:
  using Inputs = SincFilterCoefficientTable::Inputs;
  using CacheT = CoefficientTableCache<Inputs>;
  friend CacheT* CreateSincFilterCoefficientTableCache();

  static CacheT* const cache_;
  static std::vector<CacheT::SharedPtr>* persistent_cache_;

  LazySharedCoefficientTable<Inputs> filter_coefficients_;
};

}  // namespace media::audio::mixer

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_MIXER_FILTER_H_
