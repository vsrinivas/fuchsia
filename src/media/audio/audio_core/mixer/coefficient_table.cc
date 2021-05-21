// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/media/audio/audio_core/mixer/coefficient_table.h"

#include <algorithm>

namespace media::audio::mixer {

// Calculate our nearest-neighbor filter. With it we perform frame-rate conversion.
std::unique_ptr<CoefficientTable> PointFilterCoefficientTable::Create(Inputs inputs) {
  CoefficientTableBuilder table(inputs.side_length, inputs.num_frac_bits);

  // kHalfFrameIdx should always be the last idx in the filter table, because our ctor sets
  // side_length to (1 << (num_frac_bits - 1)), which == (frac_size >> 1)
  const int64_t kHalfFrameIdx = 1 << (inputs.num_frac_bits - 1);  // frac_half
  FX_CHECK(inputs.side_length == kHalfFrameIdx + 1);

  // Just a rectangular window, with the exact midpoint performing averaging (for zero phase).
  for (auto idx = 0; idx < kHalfFrameIdx; ++idx) {
    table[idx] = 1.0f;
  }

  // Here we average, so that we are zero-phase
  table[kHalfFrameIdx] = 0.5f;

  return table.Build();
}

// Calculate our linear-interpolation filter. With it we perform frame-rate conversion.
std::unique_ptr<CoefficientTable> LinearFilterCoefficientTable::Create(Inputs inputs) {
  CoefficientTableBuilder table(inputs.side_length, inputs.num_frac_bits);

  const int64_t kZeroCrossIdx = 1 << inputs.num_frac_bits;  // frac_one
  FX_CHECK(inputs.side_length == kZeroCrossIdx);

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

  return table.Build();
}

// Calculate our windowed-sinc FIR filter. With it we perform band-limited frame-rate conversion.
std::unique_ptr<CoefficientTable> SincFilterCoefficientTable::Create(Inputs inputs) {
  CoefficientTableBuilder table(inputs.side_length, inputs.num_frac_bits);

  const auto length = inputs.side_length;
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

  std::transform(table.physical_index_begin(), table.physical_index_end(),
                 table.physical_index_begin(),
                 [normalize_factor, pre_normalized_epsilon](float sample) -> float {
                   if (sample < pre_normalized_epsilon && sample > -pre_normalized_epsilon) {
                     return 0.0f;
                   }
                   return static_cast<float>(sample * normalize_factor);
                 });

  return table.Build();
}

}  // namespace media::audio::mixer
