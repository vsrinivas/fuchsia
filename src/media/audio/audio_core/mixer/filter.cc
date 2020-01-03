// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/media/audio/audio_core/mixer/filter.h"

#include "src/media/audio/lib/logging/logging.h"

namespace media::audio::mixer {

// Display the filter table values.
void Filter::DisplayTable(const std::vector<float>& filter_coefficients) {
  FX_LOGS(INFO) << "Filter: src rate " << source_rate_ << ", dest rate " << dest_rate_
                << ", width 0x" << std::hex << side_width_;

  FX_LOGS(INFO) << " **************************************************************";
  FX_LOGS(INFO) << " *** Displaying filter coefficient data for length 0x" << std::hex
                << side_width_ << "  ***";
  FX_LOGS(INFO) << " **************************************************************";

  char str[256];
  str[0] = 0;
  int n;
  for (uint32_t idx = 0; idx < side_width_; ++idx) {
    if (idx % 16 == 0) {
      FX_LOGS(INFO) << str;
      n = sprintf(str, " [%5x] ", idx);
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

float Filter::ComputeSampleFromTable(const std::vector<float>& filter_coefficients,
                                     uint32_t frac_offset, float* center) {
  FX_DCHECK(frac_offset <= frac_size_) << frac_offset;

  float result = 0.0f;

  // Negative side first
  auto sample_ptr = center;
  int32_t source_idx = 0;
  for (auto off = frac_offset; off < side_width_; off += frac_size_) {
    auto contribution = (*sample_ptr) * filter_coefficients[off];
    if constexpr (kTraceComputation) {
      FX_LOGS(INFO) << "Adding src[" << source_idx << "] " << (*sample_ptr) << " x flt[" << off
                    << "] " << filter_coefficients[off] << " = " << contribution;
    }
    result += contribution;
    --sample_ptr;
    --source_idx;
  }

  // Then positive side
  sample_ptr = center + 1;
  source_idx = 1;
  for (auto off = frac_size_ - frac_offset; off < side_width_; off += frac_size_) {
    auto contribution = (*sample_ptr) * filter_coefficients[off];
    if constexpr (kTraceComputation) {
      FX_LOGS(INFO) << "Adding src[" << source_idx << "] " << (*sample_ptr) << " x flt[" << off
                    << "] " << filter_coefficients[off] << " = " << contribution;
    }
    result += contribution;
    ++sample_ptr;
    ++source_idx;
  }
  if constexpr (kTraceComputation) {
    FX_LOGS(INFO) << "... to get " << result;
  }
  return result;
}

// PointFilter
//
// Calculate our nearest-neighbor filter. With it we perform frame-rate conversion.
void PointFilter::SetUpFilterCoefficients() {
  auto width = side_width();
  filter_coefficients_.resize(width, 0.0f);

  // We need not account for rate_conversion_ratio here.
  auto transition_idx = frac_size() >> 1u;

  // We know that transition_idx will always be the last idx in the filter table, because in our
  // ctor we set side_width to (1u << (num_frac_bits - 1u)) + 1u, which == (frac_size() >> 1u) + 1u
  FX_DCHECK(transition_idx + 1u == width);

  // Just a rectangular window, actually.
  for (auto idx = 0u; idx < transition_idx; ++idx) {
    filter_coefficients_[idx] = 1.0f;
  }

  // Here we average, so that we are zero-phase
  filter_coefficients_[transition_idx] = 0.5f;

  for (auto idx = transition_idx + 1; idx < width; ++idx) {
    filter_coefficients_[idx] = 0.0f;
  }
}

// LinearFilter
//
// Calculate our linear-interpolation filter. With it we perform frame-rate conversion.
void LinearFilter::SetUpFilterCoefficients() {
  auto width = side_width();
  filter_coefficients_.resize(width, 0.0f);

  // We need not account for rate_conversion_ratio here.
  uint32_t transition_idx = frac_size();

  // Just a Bartlett (triangular) window, actually.
  for (auto idx = 0u; idx < transition_idx; ++idx) {
    auto factor = static_cast<float>(transition_idx - idx) / transition_idx;

    if (factor >= std::numeric_limits<float>::epsilon() ||
        factor <= -std::numeric_limits<float>::epsilon()) {
      filter_coefficients_[idx] = factor;
    } else {
      filter_coefficients_[idx] = 0.0f;
    }
  }
  for (auto idx = transition_idx; idx < width; ++idx) {
    filter_coefficients_[idx] = 0.0f;
  }
}

// SincFilter
//
// Calculate our windowed-sinc FIR filter. With it we perform band-limited frame-rate conversion.
void SincFilter::SetUpFilterCoefficients() {
  auto width = side_width();
  filter_coefficients_.resize(width, 0.0f);

  auto frac_one = frac_size();

  // By capping this at 1.0, we set our low-pass filter to the lower of [src_rate, dest_rate].
  auto conversion_rate = M_PI * fmin(rate_conversion_ratio(), 1.0);

  // First calculate our sinc filter, based on rate-conversion ratio and filter width.
  //
  // TODO(mpuryear): Make the low-pass a BAND-pass, for DC offset removal (perhaps up to 20 hz?)
  for (auto idx = 1u; idx < width; ++idx) {
    auto double_idx = static_cast<double>(idx);
    auto idx_over_frac_one = double_idx / frac_one;
    auto theta = idx_over_frac_one * conversion_rate;
    auto sin_theta = sin(theta);
    auto sinc_theta = sin_theta / theta;

    FX_VLOGS(SPEW) << "Sinc[" << std::hex << idx << "] -- Factors 1:" << idx_over_frac_one
                   << ", 2:" << theta << ", 3:" << sin_theta << ", 4:" << sinc_theta;

    // Then window the filter. Here we choose a VonHann window, but Kaiser or others can work too.
    //
    // TODO(mpuryear): Pre-populate a static VonHann|Blackman|Kaiser; don't recalc for every mixer.
    auto fraction_width = double_idx / width;
    auto pi_fraction_width = fraction_width * M_PI;
    auto cos_pi_frac_width = cos(pi_fraction_width);
    auto raised_cosine = cos_pi_frac_width * 0.5 + 0.5;

    FX_VLOGS(SPEW) << "VonHann window[" << std::hex << idx
                   << "] -- Fraction of width:" << fraction_width
                   << ", PI * fraction_width:" << pi_fraction_width
                   << ", COS(PI * fraction_width):" << cos_pi_frac_width
                   << ", Raised-cosine (result):" << raised_cosine;

    filter_coefficients_[idx] = sinc_theta * raised_cosine;
  }

  filter_coefficients_[0] = 1.0f;

  // Normalize our filter so that it doesn't change amplitude for DC (0 hz).
  // While doing this, zero out any denormal float values as an optimization.
  auto amplitude_at_dc = 0.0;
  for (auto idx = frac_one; idx < width; idx += frac_one) {
    amplitude_at_dc += filter_coefficients_[idx];
  }
  amplitude_at_dc = (2 * amplitude_at_dc) + filter_coefficients_[0];
  auto normalize_factor = 1.0 / amplitude_at_dc;

  std::transform(filter_coefficients_.begin(), filter_coefficients_.end(),
                 filter_coefficients_.begin(), [normalize_factor](float sample) -> float {
                   if (normalize_factor != 1.0) {
                     sample *= normalize_factor;
                   }
                   if (sample < std::numeric_limits<float>::epsilon() &&
                       sample > -std::numeric_limits<float>::epsilon()) {
                     return 0.0f;
                   }
                   return sample;
                 });
}

}  // namespace media::audio::mixer
