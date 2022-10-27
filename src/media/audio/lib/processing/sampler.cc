// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/lib/processing/sampler.h"

#include <lib/syslog/cpp/macros.h>
#include <lib/trace/event.h>
#include <lib/zx/time.h>

#include <cstdint>
#include <limits>

#include "src/media/audio/lib/format2/fixed.h"
#include "src/media/audio/lib/format2/format.h"
#include "src/media/audio/lib/processing/point_sampler.h"
#include "src/media/audio/lib/processing/sinc_sampler.h"
#include "src/media/audio/lib/timeline/timeline_function.h"

namespace media_audio {

void Sampler::State::AdvanceAllPositionsTo(int64_t dest_target_frame) {
  AdvancePositionsBy(dest_target_frame - next_dest_frame_, /*advance_source_pos_modulo=*/true);
}

void Sampler::State::AdvanceAllPositionsBy(int64_t dest_frames) {
  AdvancePositionsBy(dest_frames, /*advance_source_pos_modulo=*/true);
}

void Sampler::State::UpdateRunningPositionsBy(int64_t dest_frames) {
  AdvancePositionsBy(dest_frames, /*advance_source_pos_modulo=*/false);
}

void Sampler::State::ResetSourceStride(
    const media::TimelineRate& source_frac_frame_per_dest_frame) {
  if constexpr (kTracePositionEvents) {
    TRACE_DURATION("audio", __func__, "step_size_modulo", step_size_modulo_,
                   "step_size_denominator", step_size_denominator_);
  }

  step_size_ = Fixed::FromRaw(source_frac_frame_per_dest_frame.Scale(1));
  // Now that we have a new step size, calculate the new step size modulo and denominator values to
  // account for the limitations of `step_size_`.
  step_size_modulo_ = source_frac_frame_per_dest_frame.subject_delta() -
                      (source_frac_frame_per_dest_frame.reference_delta() * step_size_.raw_value());
  const uint64_t new_step_size_denominator = source_frac_frame_per_dest_frame.reference_delta();
  FX_CHECK(new_step_size_denominator > 0)
      << "new_step_size_denominator: " << new_step_size_denominator;
  FX_CHECK(step_size_modulo_ < new_step_size_denominator)
      << "step_size_modulo: " << step_size_modulo_
      << ", new_step_size_denominator: " << new_step_size_denominator;

  // Only rescale `source_pos_modulo_` if `step_size_denominator_` changes, unless the new rate is
  // zero (even if they requested a different denominator). That way we largely retain our running
  // sub-frame fraction, across step size modulo and denominator changes.
  if (new_step_size_denominator != step_size_denominator_ && step_size_modulo_ > 0) {
    // Ensure that `new_source_pos_modulo / new_step_size_denominator == source_pos_modulo_ /
    // step_size_denominator_`, which means `new_source_pos_modulo = source_pos_modulo_ *
    // new_step_size_denominator / step_size_denominator_`. For higher precision, round the result
    // by adding "1/2":
    //
    //   ```
    //   new_source_pos_modulo =
    //       floor((source_pos_modulo_ * new_step_size_denominator / step_size_denominator_) + 1/2)
    //   ```
    //
    // Avoid float math and floor, and let int-division do the truncation for us:
    //
    //   ```
    //   new_source_pos_modulo =
    //       (source_pos_modulo_ * new_step_size_denominator + step_size_denominator_ / 2) /
    //       step_size_denominator_
    //   ```
    //
    // The max `source_pos_modulo_` is `UINT64_MAX - 1`. New and old denominators should never be
    // equal; but even if both are `UINT64_MAX`, the maximum `source_pos_modulo_ *
    // new_step_size_denominator` product is `< UINT128_MAX - UINT64_MAX`. Even after adding
    // `UINT64_MAX / 2 (for rounding), `new_source_pos_modulo` cannot overflow its `uint128_t`.
    //
    // Since `source_pos_modulo_ < step_size_denominator_`, our conceptual "+1/2" for rounding could
    // only make `new_source_pos_modulo` eequal to `step_size_denominator_`, but never exceed it. So
    // our new `source_pos_modulo_` cannot overflow its `uint64_t`.
    __uint128_t new_source_pos_modulo =
        static_cast<__uint128_t>(source_pos_modulo_) * new_step_size_denominator;
    new_source_pos_modulo += static_cast<__uint128_t>(step_size_denominator_ / 2);
    new_source_pos_modulo /= static_cast<__uint128_t>(step_size_denominator_);

    if (static_cast<uint64_t>(new_source_pos_modulo) == new_step_size_denominator) {
      new_source_pos_modulo = 0;
      next_source_frame_ += Fixed::FromRaw(1);
    }
    source_pos_modulo_ = static_cast<uint64_t>(new_source_pos_modulo);
    step_size_denominator_ = new_step_size_denominator;
    FX_CHECK(source_pos_modulo_ < step_size_denominator_)
        << "source_pos_modulo: " << source_pos_modulo_
        << ", new_step_size_denominator: " << step_size_denominator_;
  }
}

int64_t Sampler::State::DestFromSourceLength(Fixed source_length) const {
  FX_DCHECK(source_length >= Fixed(0));
  FX_DCHECK(step_size_ >= Fixed::FromRaw(1));
  FX_DCHECK(step_size_denominator_ > 0);
  FX_DCHECK(step_size_modulo_ < step_size_denominator_);
  FX_DCHECK(source_pos_modulo_ < step_size_denominator_);

  if (step_size_modulo_ == 0) {
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
  // The largest possible `step_size_` and `step_size_denominator_` still leave more than enough
  // room for the max possible `step_size_modulo_`, and the largest possible `step_size_rebased`
  // exceeds the largest possible `source_length_rebased`.
  const auto source_length_rebased =
      static_cast<__int128_t>(source_length.raw_value()) * step_size_denominator_ -
      source_pos_modulo_;
  const auto step_size_rebased =
      static_cast<__int128_t>(step_size_.raw_value()) * step_size_denominator_ + step_size_modulo_;

  // We know this `DCHECK` holds, because if we divide both top and bottom by
  // `step_size_denominator_`, then top is `std::numeric_limits<int64_t>::max()` or less, and bottom
  // is 1 or more.
  FX_DCHECK(source_length_rebased / step_size_rebased <= std::numeric_limits<int64_t>::max());

  auto steps = static_cast<int64_t>(source_length_rebased / step_size_rebased);
  if (source_length_rebased % step_size_rebased) {
    ++steps;
  }
  return steps;
}

Fixed Sampler::State::SourceFromDestLength(int64_t dest_length) const {
  // `step_size_modulo_` and `step_size_denominator_` are both arbitrarily large 64-bit types, so we
  // must up-cast to 128-bit.
  const auto running_modulo =
      static_cast<__int128_t>(step_size_modulo_) * static_cast<__int128_t>(dest_length) +
      static_cast<__int128_t>(source_pos_modulo_);
  // But `step_size_modulo_` and `source_pos_modulo_` are both `< step_size_denominator_`, so
  // `mod_contribution <= dest_length`.
  const __int128_t mod_contribution =
      running_modulo / static_cast<__int128_t>(step_size_denominator_);
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

// This method resets long-running and per-Mix position counters, called when a destination
// discontinuity occurs. It sets next_dest_frame_ to the specified value and calculates
// next_source_frame_ based on the dest_frames_to_frac_source_frames transform.
void Sampler::State::ResetPositions(
    int64_t target_dest_frame, const media::TimelineFunction& dest_frames_to_frac_source_frames) {
  if constexpr (kTracePositionEvents) {
    TRACE_DURATION("audio", __func__, "target_dest_frame", target_dest_frame);
  }
  next_dest_frame_ = target_dest_frame;
  next_source_frame_ = Fixed::FromRaw(dest_frames_to_frac_source_frames.Apply(target_dest_frame));
  source_pos_error_ = zx::duration(0);
  source_pos_modulo_ = 0;
}

zx::time Sampler::State::MonoTimeFromRunningSource(
    const media::TimelineFunction& clock_mono_to_frac_source_frames) const {
  FX_DCHECK(source_pos_modulo_ < step_size_denominator_);

  const __int128_t frac_source_from_offset =
      static_cast<__int128_t>(next_source_frame_.raw_value()) -
      clock_mono_to_frac_source_frames.subject_time();

  // The calculation that would first overflow a `int128` is the partial calculation:
  //    `frac_source_from_offset * step_size_denominator * reference_delta`
  //
  // For our passed-in params, the maximal step_size_denominator that will *not* overflow is:
  //    `MAX_INT128 / abs(frac_source_from_offset) / reference_delta`
  //
  // `__int128_t` doesn't have an `abs` implementation right now so we do it manually. We add one
  // fractional frame to accommodate any `source_pos_modulo_` contribution.
  const __int128_t abs_frac_source_from_offset =
      (frac_source_from_offset < 0 ? -frac_source_from_offset : frac_source_from_offset) + 1;
  const __int128_t max_step_size_denominator = std::numeric_limits<__int128_t>::max() /
                                               abs_frac_source_from_offset /
                                               clock_mono_to_frac_source_frames.reference_delta();

  __int128_t source_pos_modulo_128 = static_cast<__int128_t>(source_pos_modulo_);
  __int128_t step_size_denominator_128 = static_cast<__int128_t>(step_size_denominator_);

  // A minimum step_size_denominator of 2 allows us to round to the nearest nsec, rather than floor.
  if (step_size_denominator_128 == 1) {
    step_size_denominator_128 = 2;
    // If step_size_denominator is 1 then `source_pos_modulo_128` is 0, so no point in doubling it.
  } else {
    // If step_size_denominator is large enough to cause overflow, scale it down for this
    // calculation.
    while (step_size_denominator_128 > max_step_size_denominator) {
      step_size_denominator_128 >>= 1;
      source_pos_modulo_128 >>= 1;
    }
    // While scaling down, don't let `source_pos_modulo_128` become equal to
    // `step_size_denominator_128`.
    source_pos_modulo_128 = std::min(source_pos_modulo_128, step_size_denominator_128 - 1);
  }

  // First portion of our `TimelineFunction::Apply`.
  const __int128_t frac_src_modulo =
      frac_source_from_offset * step_size_denominator_128 + source_pos_modulo_128;

  // Middle portion, including rate factors.
  __int128_t mono_modulo = frac_src_modulo * clock_mono_to_frac_source_frames.reference_delta();
  mono_modulo /= clock_mono_to_frac_source_frames.subject_delta();

  // Final portion, including adding in the mono offset.
  const __int128_t mono_offset_modulo =
      static_cast<__int128_t>(clock_mono_to_frac_source_frames.reference_time()) *
      step_size_denominator_128;
  mono_modulo += mono_offset_modulo;

  // While reducing from `mono_modulo` to nsec, we add `step_size_denominator_128 / 2` in order to
  // round.
  const __int128_t final_mono =
      (mono_modulo + step_size_denominator_128 / 2) / step_size_denominator_128;
  // `final_mono` is 128-bit so we can double-check that we haven't overflowed. But we reduced
  // `step_size_denominator_128` as needed to avoid all overflows.
  FX_DCHECK(final_mono <= std::numeric_limits<int64_t>::max() &&
            final_mono >= std::numeric_limits<int64_t>::min())
      << "0x" << std::hex << static_cast<uint64_t>(final_mono >> 64) << "'"
      << static_cast<uint64_t>(final_mono & std::numeric_limits<uint64_t>::max());

  return zx::time(static_cast<zx_time_t>(final_mono));
}

void Sampler::State::AdvancePositionsBy(int64_t dest_frames, bool advance_source_pos_modulo) {
  FX_CHECK(dest_frames >= 0) << "Unexpected negative advance:"
                             << " dest_frames=" << dest_frames
                             << " denom=" << step_size_denominator_
                             << " rate_mod=" << step_size_modulo_ << " "
                             << " source_pos_mod=" << source_pos_modulo_;

  int64_t frac_source_frame_delta = step_size_.raw_value() * dest_frames;
  if constexpr (kTracePositionEvents) {
    TRACE_DURATION("audio", __func__, "dest_frames", dest_frames, "advance_source_pos_modulo",
                   advance_source_pos_modulo, "frac_source_frame_delta", frac_source_frame_delta);
  }

  if (step_size_modulo_ > 0) {
    // `step_size_modulo_` and `source_pos_modulo_` can be as large as `UINT64_MAX - 1`, so we use
    // 128-bit to avoid overflow.
    const __int128_t step_size_denominator_128 = step_size_denominator_;
    __int128_t source_pos_modulo_128 = static_cast<__int128_t>(step_size_modulo_) * dest_frames;
    if (advance_source_pos_modulo) {
      source_pos_modulo_128 += source_pos_modulo_;
    }

    const uint64_t new_source_pos_modulo =
        static_cast<uint64_t>(source_pos_modulo_128 % step_size_denominator_128);
    if (advance_source_pos_modulo) {
      source_pos_modulo_ = new_source_pos_modulo;
    } else {
      // `source_pos_modulo_` has already been advanced; it is already at its eventual value.
      // `new_source_pos_modulo` is what `source_pos_modulo` would have become, if it had started at
      // zero. Now advance `source_pos_modulo_128` by the difference (which is what its initial
      // value must have been), just in case this causes `frac_source_frame_delta` to increment.
      source_pos_modulo_128 += source_pos_modulo_;
      source_pos_modulo_128 -= new_source_pos_modulo;
      if (source_pos_modulo_ < new_source_pos_modulo) {
        source_pos_modulo_128 += step_size_denominator_128;
      }
    }
    frac_source_frame_delta +=
        static_cast<int64_t>(source_pos_modulo_128 / step_size_denominator_128);
  }
  next_source_frame_ = Fixed::FromRaw(next_source_frame_.raw_value() + frac_source_frame_delta);
  next_dest_frame_ += dest_frames;
  if constexpr (kTracePositionEvents) {
    TRACE_DURATION("audio", "AdvancePositionsBy End", "nest_source_frame",
                   next_source_frame_.Integral().Floor(), "next_source_frame_.frac",
                   next_source_frame_.Fraction().raw_value(), "next_dest_frame_", next_dest_frame_,
                   "source_pos_modulo", source_pos_modulo_);
  }
}

std::shared_ptr<Sampler> Sampler::Create(const Format& source_format, const Format& dest_format,
                                         Type type) {
  TRACE_DURATION("audio", "Sampler::Create");

  if (type == Type::kDefault &&
      source_format.frames_per_second() == dest_format.frames_per_second()) {
    return PointSampler::Create(source_format, dest_format);
  }
  return SincSampler::Create(source_format, dest_format);
}

}  // namespace media_audio
