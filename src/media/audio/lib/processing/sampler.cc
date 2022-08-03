// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/lib/processing/sampler.h"

#include <lib/trace/event.h>

#include <cstdint>
#include <limits>

#include "lib/syslog/cpp/macros.h"
#include "src/media/audio/lib/format2/format.h"
#include "src/media/audio/lib/processing/point_sampler.h"
#include "src/media/audio/lib/processing/sinc_sampler.h"

namespace media_audio {

std::unique_ptr<Sampler> Sampler::Create(const Format& source_format, const Format& dest_format,
                                         Type type) {
  TRACE_DURATION("audio", "Sampler::Create");

  switch (type) {
    case Type::kDefault:
      if (source_format.frames_per_second() == dest_format.frames_per_second()) {
        return PointSampler::Create(source_format, dest_format);
      }
      return SincSampler::Create(source_format, dest_format);
    case Type::kPointSampler:
      return PointSampler::Create(source_format, dest_format);
    case Type::kSincSampler:
      return SincSampler::Create(source_format, dest_format);
  }
}

Fixed Sampler::State::SetRateModuloAndDenominator(uint64_t rate_modulo, uint64_t denominator,
                                                  Fixed source_pos_modulo) {
  if constexpr (kTracePositionEvents) {
    TRACE_DURATION("audio", __func__, "rate_modulo", rate_modulo, "denominator", denominator);
  }
  FX_CHECK(denominator > 0);
  FX_CHECK(rate_modulo < denominator);
  FX_CHECK(denominator_ > 0) << "denominator: " << denominator_;
  FX_CHECK(rate_modulo_ < denominator_)
      << "rate_modulo: " << rate_modulo_ << ", denominator: " << denominator_;
  FX_CHECK(source_pos_modulo_ < denominator_)
      << "source_pos_modulo: " << source_pos_modulo_ << ", denominator: " << denominator_;

  // Only rescale `source_pos_modulo_` if `denominator_` changes, unless the new rate is zero (even
  // if they requested a different denominator). That way we largely retain our running sub-frame
  // fraction, across `rate_modulo` and `denominator` changes.
  if (denominator != denominator_ && rate_modulo) {
    // Ensure that `new_source_pos_mod / denominator == source_pos_modulo_ / denominator_`, which
    // means `new_source_pos_mod = source_pos_modulo_ * denominator / denominator_`.
    // For higher precision, round the result by adding "1/2":
    //   `new_source_pos_mod = floor((source_pos_modulo_ * denominator / denominator_) + 1/2)`
    // Avoid float math and floor, and let int-division do the truncation for us:
    //   `new_source_pos_mod = (source_pos_modulo_ * denominator + denominator_ / 2) / denominator_`
    //
    // The max `source_pos_modulo_` is `UINT64_MAX - 1`. New and old denominators should never be
    // equal; but even if both are `UINT64_MAX`, the maximum `source_pos_modulo_ * denominator`
    // product is `< UINT128_MAX - UINT64_MAX`. Even after adding `UINT64_MAX / 2 (for rounding),
    // `new_source_pos_mod` cannot overflow its `uint128_t`.
    //
    // `source_pos_modulo_` is strictly `< denominator_`. Our conceptual "+1/2" for rounding could
    // only make `new_source_pos_mod` EQUAL to `denominator_`, never exceed it. So our new
    // `source_pos_modulo_` cannot overflow its `uint64_t`.
    __uint128_t new_source_pos_mod = static_cast<__uint128_t>(source_pos_modulo_) * denominator;
    new_source_pos_mod += static_cast<__uint128_t>(denominator_ / 2);
    new_source_pos_mod /= static_cast<__uint128_t>(denominator_);

    if (static_cast<uint64_t>(new_source_pos_mod) == denominator) {
      new_source_pos_mod = 0;
      source_pos_modulo += Fixed::FromRaw(1);
    }

    source_pos_modulo_ = static_cast<uint64_t>(new_source_pos_mod);
    denominator_ = denominator;
  }
  rate_modulo_ = rate_modulo;

  return source_pos_modulo;
}

int64_t Sampler::State::DestFromSourceLength(Fixed source_length) const {
  FX_DCHECK(source_length >= Fixed(0));
  FX_DCHECK(step_size_ >= Fixed::FromRaw(1));
  FX_DCHECK(denominator_ > 0);
  FX_DCHECK(rate_modulo_ < denominator_);
  FX_DCHECK(source_pos_modulo_ < denominator_);

  if (rate_modulo_ == 0) {
    // Ceiling discards any fractional remainder less than `Fixed::FromRaw(1)` because it floors to
    // `Fixed::FromRaw(1)` precision before rounding up.
    int64_t steps = source_length.raw_value() / step_size_.raw_value();
    if (source_length > step_size_ * steps) {
      ++steps;
    }
    return steps;
  }

  // Both calculations fit into `__int128_t`; where `source_length.raw_value` and
  // `step_size.raw_value` are both `int64_t`, and the internal state values are each `uint64_t`.
  // The largest possible `step_size_` and `denominator_` still leave more than enough room for the
  // max possible `rate_modulo_`, and the largest possible `step_size_rebased` exceeds the largest
  // possible `source_length_rebased`.
  const auto source_length_rebased =
      static_cast<__int128_t>(source_length.raw_value()) * denominator_ - source_pos_modulo_;
  const auto step_size_rebased =
      static_cast<__int128_t>(step_size_.raw_value()) * denominator_ + rate_modulo_;

  // We know this `DCHECK` holds, because if we divide both top and bottom by `denominator_`, then
  // top is `std::numeric_limits<int64_t>::max()` or less, and bottom is 1 or more.
  FX_DCHECK(source_length_rebased / step_size_rebased <= std::numeric_limits<int64_t>::max());

  auto steps = static_cast<int64_t>(source_length_rebased / step_size_rebased);
  if (source_length_rebased % step_size_rebased) {
    ++steps;
  }
  return steps;
}

Fixed Sampler::State::SourceFromDestLength(int64_t dest_length) const {
  // `rate_modulo_` and `denominator_` are each 64-bit types, so we must up-cast to 128-bit.
  const auto running_modulo =
      static_cast<__int128_t>(rate_modulo_) * static_cast<__int128_t>(dest_length) +
      static_cast<__int128_t>(source_pos_modulo_);
  // But `rate_modulo` and `source_pos_modulo < denominator`, so `mod_contribution <= dest_length`.
  const __int128_t mod_contribution = running_modulo / static_cast<__int128_t>(denominator_);
  FX_DCHECK(mod_contribution <= std::numeric_limits<int64_t>::max());

  // Max `step_size_` is 192, which is 21 bits in `Fixed` (8.13). Also, `mod_contribution` cannot
  // exceed `dest_length`, which means:
  //     `source_length_raw <= (step_size_.raw_value() + 1) * dest_length`
  // Thus, `source_length_raw` will overflow an `int64_t` only if `dest_length >=
  // 2 ^ 63 / (192 * 2 ^ 13 + 1)`, which is `dest_length > 5.86e12`, which is `dest_length > 353
  // days at 192khz`.
  const int64_t source_length_raw =
      step_size_.raw_value() * dest_length + static_cast<int64_t>(mod_contribution);
  return Fixed::FromRaw(source_length_raw);
}

}  // namespace media_audio
