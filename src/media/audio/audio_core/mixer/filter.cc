// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/media/audio/audio_core/mixer/filter.h"

#include <lib/syslog/cpp/macros.h>
#include <lib/trace/event.h>
#include <lib/zx/clock.h>

#include <iomanip>
#include <memory>
#include <mutex>

#include "src/media/audio/audio_core/mixer/coefficient_table.h"

namespace media::audio::mixer {

// Display the filter table values.
void Filter::DisplayTable(const CoefficientTable& filter_coefficients) {
  FX_LOGS(INFO) << "Filter: source rate " << source_rate_ << ", dest rate " << dest_rate_
                << ", length 0x" << std::hex << side_length_;

  FX_LOGS(INFO) << " **************************************************************";
  FX_LOGS(INFO) << " *** Displaying filter coefficient data for length 0x" << std::hex
                << side_length_ << "  ***";
  FX_LOGS(INFO) << " **************************************************************";

  char str[256];
  str[0] = 0;
  int n;
  for (int64_t idx = 0; idx < side_length_; ++idx) {
    if (idx % 16 == 0) {
      FX_LOGS(INFO) << str;
      n = sprintf(str, " [%5lx] ", idx);
    }
    if (filter_coefficients[idx] < std::numeric_limits<float>::epsilon() &&
        filter_coefficients[idx] > -std::numeric_limits<float>::epsilon() &&
        filter_coefficients[idx] != 0.0f) {
      n += sprintf(str + n, "!%10.7f!", filter_coefficients[idx]);
    } else {
      n += sprintf(str + n, " %10.7f ", filter_coefficients[idx]);
    }
  }
  FX_LOGS(INFO) << str;
  FX_LOGS(INFO) << " **************************************************************";
}

// Used to debug computation of output values (ComputeSample), from coefficients and input values.
constexpr bool kTraceComputation = false;

// For frac_offset in [0.0, 1.0) we require source frames on each side depending on filter length.
// Source frames are at integral positions, but we treat frac_offset as filter center, so source
// frames appear to be fractionally positioned.
//
// Filter coefficients cover the entire discrete space of fractional positions, but any calculation
// references only a subset of these, using a one-frame stride (frac_size_). Coefficient tables
// internally store values with an integer stride contiguously, which is what these loops want.
//   Ex:
//     coefficient_ptr[1] == filter_coefficients[frac_offset + frac_size_];
//
// We first calculate the contribution of the negative side of the filter, and then the contribution
// of the positive side. To avoid double-counting it, we include center subframe 0 only in the
// negative-side calculation.
float Filter::ComputeSampleFromTable(const CoefficientTable& filter_coefficients,
                                     int64_t frac_offset, float* center) {
  FX_DCHECK(frac_offset <= frac_size_) << frac_offset;
  if constexpr (kTraceComputation) {
    FX_LOGS(INFO) << "For frac_offset " << std::hex << frac_offset << " ("
                  << (static_cast<double>(frac_offset) / static_cast<double>(frac_size_)) << "):";
  }

  // We use some raw pointers here to make loops vectorizable.
  float* sample_ptr;
  const float* coefficient_ptr;
  float result = 0.0f;

  // Negative side examples --
  // side_length 1.601, frac_offset 0.600 requires source range (-1.001, 0.600]: frames -1 and 0.
  // side_length 1.601, frac_offset 0.601 requires source range (-1.000, 0.601]: frame 0.
  int64_t source_frames = (side_length_ - 1 + frac_size_ - frac_offset) >> num_frac_bits_;
  if (source_frames > 0) {
    sample_ptr = center;
    coefficient_ptr = filter_coefficients.ReadSlice(frac_offset, source_frames);
    FX_CHECK(coefficient_ptr != nullptr);

    for (int64_t source_idx = 0; source_idx < source_frames; ++source_idx) {
      auto contribution = (*sample_ptr) * coefficient_ptr[source_idx];
      if constexpr (kTraceComputation) {
        FX_LOGS(INFO) << "Adding source[" << -static_cast<ssize_t>(source_idx) << "] "
                      << (*sample_ptr) << " x " << coefficient_ptr[source_idx] << " = "
                      << contribution;
      }
      result += contribution;
      --sample_ptr;
    }
  }

  // Positive side examples --
  // side_length 1.601, frac_offset 0.400 requires source range (0.400, 2.001): frames 1 and 2.
  // side_length 1.601, frac_offset 0.399 requires source range (0.399, 2.000): frame 1.
  //
  // Reduction of: side_length_ + (frac_size_-1) - (frac_size_-frac_offset)
  source_frames = (side_length_ - 1 + frac_offset) >> num_frac_bits_;
  if (source_frames > 0) {
    sample_ptr = center + 1;
    coefficient_ptr = filter_coefficients.ReadSlice(frac_size_ - frac_offset, source_frames);
    FX_CHECK(coefficient_ptr != nullptr);

    for (int64_t source_idx = 0; source_idx < source_frames; ++source_idx) {
      auto contribution = sample_ptr[source_idx] * coefficient_ptr[source_idx];
      if constexpr (kTraceComputation) {
        FX_LOGS(INFO) << "Adding source[" << 1 + source_idx << "] " << std::setprecision(13)
                      << sample_ptr[source_idx] << " x " << coefficient_ptr[source_idx] << " = "
                      << contribution;
      }
      result += contribution;
    }
  }

  if constexpr (kTraceComputation) {
    FX_LOGS(INFO) << "... to get " << std::setprecision(13) << result;
  }
  return result;
}

// PointFilter
//
// Calculate our nearest-neighbor filter. With it we perform frame-rate conversion.
CoefficientTable* CreatePointFilterTable(PointFilter::Inputs inputs) {
  TRACE_DURATION("audio", "CreatePointFilterTable");
  auto out = new CoefficientTable(inputs.side_length, inputs.num_frac_bits);
  auto& table = *out;

  // kHalfFrameIdx should always be the last idx in the filter table, because our ctor sets
  // side_length to (1 << (num_frac_bits - 1)), which == (frac_size >> 1)
  const int64_t kHalfFrameIdx = 1 << (inputs.num_frac_bits - 1);  // frac_half
  FX_DCHECK(inputs.side_length == kHalfFrameIdx + 1)
      << "Computed filter edge " << kHalfFrameIdx << " should equal specified side_length "
      << inputs.side_length;

  // Just a rectangular window, with the exact midpoint performing averaging (for zero phase).
  for (auto idx = 0; idx < kHalfFrameIdx; ++idx) {
    table[idx] = 1.0f;
  }

  // Here we average, so that we are zero-phase
  table[kHalfFrameIdx] = 0.5f;

  return out;
}

// LinearFilter
//
// Calculate our linear-interpolation filter. With it we perform frame-rate conversion.
CoefficientTable* CreateLinearFilterTable(LinearFilter::Inputs inputs) {
  TRACE_DURATION("audio", "CreateLinearFilterTable");
  auto out = new CoefficientTable(inputs.side_length, inputs.num_frac_bits);
  auto& table = *out;

  const int64_t kZeroCrossIdx = 1 << inputs.num_frac_bits;  // frac_one
  FX_DCHECK(inputs.side_length == kZeroCrossIdx)
      << "Computed filter edge " << kZeroCrossIdx << " should equal specified side_length "
      << inputs.side_length;

  const float kTransitionFactor = 1.0f / static_cast<float>(kZeroCrossIdx);

  // Just a Bartlett (triangular) window.
  for (auto idx = 0; idx < kZeroCrossIdx; ++idx) {
    auto factor = static_cast<float>(kZeroCrossIdx - idx) * kTransitionFactor;

    if (factor >= std::numeric_limits<float>::epsilon() ||
        factor <= -std::numeric_limits<float>::epsilon()) {
      table[idx] = factor;
    } else {
      table[idx] = 0.0f;
    }
  }

  return out;
}

// SincFilter
//
// Calculate our windowed-sinc FIR filter. With it we perform band-limited frame-rate conversion.
CoefficientTable* CreateSincFilterTable(SincFilter::Inputs inputs) {
  TRACE_DURATION("audio", "CreateSincFilterTable");
  auto start_time = zx::clock::get_monotonic();

  const auto length = inputs.side_length;
  auto out = new CoefficientTable(length, inputs.num_frac_bits);
  auto& table = *out;

  const auto frac_one = 1 << inputs.num_frac_bits;

  // By capping this at 1.0, we set our low-pass filter to the lower of [source_rate, dest_rate].
  const double conversion_rate = M_PI * fmin(inputs.rate_conversion_ratio, 1.0);

  // Construct a sinc-based LPF, from our rate-conversion ratio and filter length.
  const double theta_factor = conversion_rate / frac_one;

  // Concurrently, calculate a VonHann window function. These form the windowed-sinc filter.
  const double normalize_length_factor = M_PI / static_cast<double>(length);

  table[0] = 1.0f;
  for (auto idx = 1; idx < length; ++idx) {
    const double theta = theta_factor * idx;
    const double sinc_theta = sin(theta) / theta;

    // TODO(mpuryear): Pre-populate a static VonHann|Blackman|Kaiser window; don't recalc each one.
    const double raised_cosine = cos(normalize_length_factor * idx) * 0.5 + 0.5;

    table[idx] = static_cast<float>(sinc_theta * raised_cosine);
  }

  // Normalize our filter so that it doesn't change amplitude for DC (0 hz).
  // While doing this, zero out any denormal float values as an optimization.
  double amplitude_at_dc = 0.0;

  for (auto idx = frac_one; idx < length; idx += frac_one) {
    amplitude_at_dc += table[idx];
  }
  amplitude_at_dc = (2 * amplitude_at_dc) + table[0];

  const double normalize_factor = 1.0 / amplitude_at_dc;
  const double pre_normalized_epsilon = std::numeric_limits<float>::epsilon() * amplitude_at_dc;

  std::transform(table.begin(), table.end(), table.begin(),
                 [normalize_factor, pre_normalized_epsilon](float sample) -> float {
                   if (sample < pre_normalized_epsilon && sample > -pre_normalized_epsilon) {
                     return 0.0f;
                   }
                   return static_cast<float>(sample * normalize_factor);
                 });

  auto end_time = zx::clock::get_monotonic();
  FX_LOGS(INFO) << "CreateSincFilterTable took " << (end_time - start_time).to_nsecs()
                << " ns with Inputs { side_length=" << length
                << ", num_frac_bits=" << inputs.num_frac_bits
                << ", rate_conversion_ratio=" << inputs.rate_conversion_ratio << " }";
  return out;
}

SincFilter::CacheT* CreateSincFilterCoefficientTableCache() {
  auto cache = new SincFilter::CacheT(CreateSincFilterTable);

  auto make_inputs = [](int32_t source_rate, int32_t dest_rate) {
    return SincFilter::Inputs{
        .side_length = SincFilter::Length(source_rate, dest_rate).raw_value(),
        .num_frac_bits = Fixed::Format::FractionalBits,
        .rate_conversion_ratio = static_cast<double>(dest_rate) / static_cast<double>(source_rate),
    };
  };

  // To avoid lengthy construction time, cache some coefficient tables persistently.
  // See fxbug.dev/45074 and fxbug.dev/57666.
  SincFilter::persistent_cache_ = new std::vector<SincFilter::CacheT::SharedPtr>;
  SincFilter::persistent_cache_->push_back(cache->Get(make_inputs(48000, 48000)));
  SincFilter::persistent_cache_->push_back(cache->Get(make_inputs(96000, 48000)));
  SincFilter::persistent_cache_->push_back(cache->Get(make_inputs(48000, 96000)));
  SincFilter::persistent_cache_->push_back(cache->Get(make_inputs(96000, 16000)));
  SincFilter::persistent_cache_->push_back(cache->Get(make_inputs(44100, 48000)));
  return cache;
}

// static
PointFilter::CacheT* const PointFilter::cache_ = new PointFilter::CacheT(CreatePointFilterTable);
LinearFilter::CacheT* const LinearFilter::cache_ =
    new LinearFilter::CacheT(CreateLinearFilterTable);

// Must initialize persistent_cache_ first as it's used by the Create function.
std::vector<SincFilter::CacheT::SharedPtr>* SincFilter::persistent_cache_ = nullptr;
SincFilter::CacheT* const SincFilter::cache_ = CreateSincFilterCoefficientTableCache();

}  // namespace media::audio::mixer
