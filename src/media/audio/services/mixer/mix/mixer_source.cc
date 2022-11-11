// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/services/mixer/mix/mixer_source.h"

#include <lib/trace/event.h>
#include <lib/zx/time.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <unordered_set>
#include <utility>

#include "src/media/audio/lib/clock/clock_synchronizer.h"
#include "src/media/audio/lib/format2/fixed.h"
#include "src/media/audio/lib/format2/format.h"
#include "src/media/audio/lib/processing/gain.h"
#include "src/media/audio/lib/processing/sampler.h"
#include "src/media/audio/services/mixer/common/basic_types.h"
#include "src/media/audio/services/mixer/mix/mix_job_context.h"
#include "src/media/audio/services/mixer/mix/mixer_gain_controls.h"
#include "src/media/audio/services/mixer/mix/pipeline_stage.h"
#include "src/media/audio/services/mixer/mix/silence_padding_stage.h"

namespace media_audio {

namespace {

// Source position errors generally represent only the rate difference between time sources. We
// reconcile clocks upon every `PipelineStage::Read` call, so even with wildly divergent clocks
// (+1000ppm vs. -1000ppm) source position error would be 1/50 of the duration between `Read` calls.
// If source position error exceeds this limit, we stop rate-adjustment and instead "snap" to the
// expected position (referred to as "jam sync"). This manifests as a discontinuity or dropout for
// this source stream only.
//
// For reference, micro-SRC can smoothly eliminate errors of this duration in less than 1 second. If
// adjusting a `zx::clock`, this will take approximately 2 seconds.
constexpr zx::duration kMaxErrorThresholdDuration = zx::msec(2);

// Converts given `dest_time_to_dest_frac_frame` to transform destination time to integral frames.
TimelineFunction DestTimeToDestFrame(const TimelineFunction& dest_time_to_dest_frac_frame) {
  static const TimelineRate frames_per_fractional_frame = TimelineRate(1, kOneFrame.raw_value());
  return TimelineFunction::Compose(TimelineFunction(frames_per_fractional_frame),
                                   dest_time_to_dest_frac_frame);
}

}  // namespace

MixerSource::MixerSource(PipelineStagePtr source, PipelineStage::AddSourceOptions options,
                         const std::unordered_set<GainControlId>& dest_gain_ids,
                         int64_t max_dest_frame_count_per_mix)
    : clock_sync_(std::move(options.clock_sync)),
      dest_clock_(clock_sync_ ? (source->reference_clock() == clock_sync_->follower()
                                     ? std::move(clock_sync_->leader())
                                     : std::move(clock_sync_->follower()))
                              : nullptr),
      source_clock_(clock_sync_ ? (source->reference_clock() == clock_sync_->follower()
                                       ? std::move(clock_sync_->follower())
                                       : std::move(clock_sync_->leader()))
                                : nullptr),
      sampler_(std::move(options.sampler)),
      source_(std::make_unique<SilencePaddingStage>(
          source->format(), source->reference_clock(), source->thread(),
          sampler_->neg_filter_length() + sampler_->pos_filter_length(),
          /*round_down_fractional_frames=*/true)),
      source_gain_ids_(std::move(options.gain_ids)),
      gain_scales_(max_dest_frame_count_per_mix, kUnityGainScale) {
  FX_CHECK(clock_sync_);
  FX_CHECK(sampler_);
  FX_CHECK(max_dest_frame_count_per_mix > 0);
  SetDestGains(dest_gain_ids);
  // No need to pass `options` further, since we already consumed them above.
  source_->AddSource(std::move(source), /*options=*/{});
}

void MixerSource::Advance(MixJobContext& ctx, const TimelineFunction& dest_time_to_dest_frac_frame,
                          Fixed dest_frame) {
  const auto dest_time =
      zx::time{dest_time_to_dest_frac_frame.ApplyInverse(dest_frame.raw_value())};
  const auto mono_time = dest_clock_->MonotonicTimeFromReferenceTime(dest_time);
  const auto source_frame =
      source_->FrameFromPresentationTime(source_clock_->ReferenceTimeFromMonotonicTime(mono_time));
  source_->Advance(ctx, source_frame);
}

bool MixerSource::Mix(MixJobContext& ctx, const TimelineFunction& dest_time_to_dest_frac_frame,
                      Fixed dest_start_frame, int64_t dest_frame_count, float* dest_samples,
                      bool accumulate) {
  UpdateSamplerState(dest_time_to_dest_frac_frame, dest_start_frame.Floor());
  auto& state = sampler_->state();

  // We use filter "width", as opposed to filter "length", which excludes the filters' center point
  // for simpler frame position calculations.
  const Fixed pos_filter_width = sampler_->pos_filter_length() - Fixed::FromRaw(1);
  const Fixed neg_filter_width = sampler_->neg_filter_length() - Fixed::FromRaw(1);

  int64_t dest_frame_offset = 0;
  while (dest_frame_offset < dest_frame_count) {
    const int64_t prev_dest_frame_offset = dest_frame_offset;

    auto packet = ReadNextSourcePacket(ctx, dest_frame_count - dest_frame_offset);
    if (!packet) {
      break;
    }

    if constexpr (kTracePositionEvents) {
      TRACE_DURATION("audio", "MixerSource::Mix position", "start",
                     packet->start_frame().Integral().Floor(), "start.frac",
                     packet->start_frame().Fraction().raw_value(), "length", packet->frame_count(),
                     "next_source_frame", state.next_source_frame().Integral().Floor(),
                     "next_source_frame.frac", state.next_source_frame().Fraction().raw_value(),
                     "dest_frame_offset", dest_frame_offset, "dest_frame_count", dest_frame_count);
    }

    // We start sampling at `state.next_source_frame`, compute the frame offset for `packet`.
    Fixed source_frame_offset = state.next_source_frame() - packet->start_frame();

    // To compute the destination frame D centered at source frame S, we use frames from a window
    // surrounding S, defined by the positive and negative filter widths. For example, if we are
    // down-sampling, the streams may look like:
    //
    // ```
    //    source stream      ++++++++++++++S++++++++++++++++++++++
    //                               |     ^     |
    //                               +-----+-----+
    //                    neg_filter_width | pos_filter_width
    //                                     |
    //                                     V
    //    destination stream +   +   +   + D +   +   +   +   +   +
    // ```
    //
    // At this point in the code, `D = dest_frame_offset` and `S = state.next_source_frame`. This is
    // our starting point. There are two interesting cases:
    //
    //  1. `S - 1.0 < packet->start_frame() <= S + pos_filter_width`
    //
    //     The first packet frame can be used to produce frame D. This is the common case for
    //     continuous (i.e. gapless) streams of audio. In this case, `sampler_` has cached all
    //     source frames in the range `[S - neg_filter_width, X - 1]`, where `X =
    //     packet->start_frame()`. We combine those cached frames with the first `S +
    //     pos_filter_width - X` frames from the packet to produce D.
    //
    //  2. `packet->start_frame() > S + pos_filter_width`
    //
    //     The first packet frame is beyond the last frame needed to produce frame D. This means
    //     there is a gap in the source stream. Since our source is wrapped with a
    //     `SilencePaddingStage`, there must have been at least `neg_filter_width +
    //     pos_filter_width` silent frames before that gap, hence our sampler has quiesced to a
    //     "silent" state and will fill that gap with silence. This implies that all frames in the
    //     range `[S - neg_filter_width, S + pos_filter_width]` are silent, and hence D is silent as
    //     well. Since `dest_frame_count` should be zeroed before we start mixing, we don't need to
    //     produce frame D. Instead we advance `dest_frame_offset` to the first frame D' whose
    //     sampling window includes packet->start_frame().
    int64_t dest_frames_to_advance = 0;
    if (packet->start_frame() > state.next_source_frame() + pos_filter_width) {
      // To illustrate:
      //
      // ```
      //    source stream      ++S+++++++++++++++++++++++++++++++++++++++++++S'++++X++++++++++++
      //                         ^     |                               |     ^     |
      //                         +-----+                               +-----+-----+
      //                         | pos_filter_width         neg_filter_width | pos_filter_width
      //                         |                                           |
      //                         V                                           V
      //    destination stream + D +   +   +   +   +   +   +   +   +   +   + D'+   +   +   +   +
      //
      // S  = current source position (state.next_source_frame())
      // X  = packet->start_frame()
      // D  = current destination position (dest_frame_offset)
      // D' = first destination frame whose sampling window overlaps with packet->start_frame()
      // S' = source position after advancing to D'
      // ```

      // We need to advance at least this many source frames.
      const auto mix_to_packet_gap =
          Fixed(packet->start_frame() - state.next_source_frame() - pos_filter_width);

      // We need to advance this many destination frames to find a D' as illustrated above, but
      // don't advance past the end of the destination buffer.
      dest_frames_to_advance = state.DestFromSourceLength(mix_to_packet_gap);
      dest_frames_to_advance =
          std::clamp(dest_frames_to_advance, 0l, dest_frame_count - dest_frame_offset);

      // Advance our long-running positions.
      const auto initial_next_source_frame = state.next_source_frame();
      const auto initial_source_pos_modulo = state.source_pos_modulo();
      state.AdvanceAllPositionsBy(dest_frames_to_advance);

      // Advance our local offsets. We advance the `source_frame_offset` the same amount as we
      // advanced `state.next_source_frame`.
      dest_frame_offset += dest_frames_to_advance;
      source_frame_offset =
          Fixed(source_frame_offset + state.next_source_frame() - initial_next_source_frame);

      if constexpr (kTracePositionEvents) {
        TRACE_DURATION("audio", "MixerSource::Mix dest_frames_to_advance", "dest_frames_to_advance",
                       dest_frames_to_advance);
      }

      FX_CHECK(source_frame_offset + pos_filter_width >= Fixed(0))
          << "source_frame_offset (" << ffl::String::DecRational << source_frame_offset
          << ") + pos_width (" << Fixed(-pos_filter_width)
          << ") should >= 0 -- source running position was " << initial_next_source_frame << " (+ "
          << initial_source_pos_modulo << "/" << state.step_size_denominator()
          << " modulo), is now " << state.next_source_frame() << " (+ " << state.source_pos_modulo()
          << "/" << state.step_size_denominator() << " modulo); advanced dest by "
          << dest_frames_to_advance;

      FX_CHECK(dest_frame_offset <= dest_frame_count)
          << ffl::String::DecRational << "dest_frame_offset " << dest_frame_offset
          << " advanced by " << dest_frames_to_advance << " to " << dest_frame_count
          << ", exceeding " << dest_frame_count << ";"
          << " mix_to_packet_gap=" << mix_to_packet_gap << " step_size=" << state.step_size()
          << " step_size_modulo=" << state.step_size_modulo()
          << " step_size_denominator=" << state.step_size_denominator()
          << " source_pos_modulo=" << state.source_pos_modulo() << " (was "
          << initial_source_pos_modulo << ")";
    }

    // It is guaranteed here that `dest_frame_offset <= dest_frame_count` (see `FX_CHECK` above).
    if (dest_frame_offset == dest_frame_count) {
      // We skipped so many frames in the destination buffer that we overran the end of the buffer,
      // which means that we are already done with this mix job. This can happen when there is a
      // large gap between our initial source position and `packet->start_frame()`.
      packet->set_frames_consumed(0);
    } else if (Fixed(source_frame_offset) - neg_filter_width >= Fixed(packet->frame_count())) {
      // The source packet was initially within our mix window, but after skipping destination
      // frames, it is now entirely in the past. This can only occur when down-sampling and is
      // made more likely if the rate conversion ratio is very high. In the example below, D and S
      // are the initial destination and source positions, D' and S' are the new positions after
      // skipping destination frames, and X marks the source packet, which is not in the sampling
      // window for either D or D'.
      //
      // ```
      //    source stream      ++++++++++++++S++++++++++++XXXXXXXXXXXX++++++++++++S'+++++
      //                               |     ^     |                        |     ^     |
      //                               +-----+-----+                        +-----+-----+
      //                     neg_filter_with | pos_filter_with    neg_filter_with |
      //                     pos_filter_with
      //                                     |                                    |
      //                                     V                                    V
      //    destination stream +             D                  +                 D'
      // ```
      packet->set_frames_consumed(packet->frame_count());
    } else {
      const int64_t dest_frame_offset_before_mix = dest_frame_offset;
      MixJobSubtask subtask("MixerSource::Mix");
      sampler_->Process({packet->payload(), &source_frame_offset, packet->frame_count()},
                        {dest_samples, &dest_frame_offset, dest_frame_count}, gain_, accumulate);
      subtask.Done();
      ctx.AddSubtaskMetrics(subtask.FinalMetrics());

      packet->set_frames_consumed(
          std::min(Fixed(source_frame_offset + pos_filter_width).Floor(), packet->frame_count()));

      // Check that we did not overflow the buffer.
      FX_CHECK(dest_frame_offset <= dest_frame_count)
          << ffl::String::DecRational
          << "dest_frame_offset(before)=" << dest_frame_offset_before_mix
          << " dest_frame_offset(after)=" << dest_frame_offset
          << " dest_frame_count=" << dest_frame_count << " packet.start=" << packet->start_frame()
          << " packet.length=" << packet->frame_count()
          << " source_frame_offset(final)=" << source_frame_offset;
    }

    // Advance positions by the number of mixed frames. Note that we have already advanced by
    // `dest_frames_to_advance`.
    state.UpdateRunningPositionsBy(dest_frame_offset - prev_dest_frame_offset -
                                   dest_frames_to_advance);
  }

  // If there was insufficient supply to meet our demand, we may not have mixed enough frames, but
  // we advance our destination frame count as if we did, because time rolls on.
  state.AdvanceAllPositionsTo(dest_start_frame.Floor() + dest_frame_count);

  // Return true if we mixed at least one frame that was not silenced by the source gain.
  return gain_.type != GainType::kSilent && dest_frame_offset > 0;
}

void MixerSource::PrepareSourceGainForNextMix(MixJobContext& ctx,
                                              const MixerGainControls& gain_controls,
                                              const TimelineFunction& dest_time_to_dest_frac_frame,
                                              int64_t dest_frame_offset, int64_t dest_frame_count) {
  FX_CHECK(dest_frame_offset == 0 ||
           (last_prepared_gain_frame_ && dest_frame_offset >= *last_prepared_gain_frame_));
  last_prepared_gain_frame_ = dest_frame_offset + dest_frame_count;

  const auto dest_frame_to_mono_time =
      dest_clock_->to_clock_mono() * DestTimeToDestFrame(dest_time_to_dest_frac_frame).Inverse();

  float gain_db = kUnityGainDb;
  bool is_ramping = false;
  for (const auto& gain_id : all_gain_ids_) {
    const auto& gain_control = gain_controls.Get(gain_id);
    const auto& state = gain_control.state();
    if (state.is_muted || state.gain_db <= kMinGainDb) {
      // Gain is silent.
      gain_db = kMinGainDb;
      break;
    }

    if (is_ramping || state.linear_scale_slope_per_ns != 0.0f) {
      // Gain is ramping.
      if (!is_ramping) {
        is_ramping = true;
        std::fill(&gain_scales_[dest_frame_offset], &gain_scales_[dest_frame_count],
                  DbToScale(gain_db));
      }
      // Calculate the ramp increment per frame.
      const auto gain_control_clock = ctx.clocks().SnapshotFor(gain_control.reference_clock());
      const auto gain_control_ns_per_dest_frame = TimelineRate::Product(
          dest_frame_to_mono_time.rate(), gain_control_clock.to_clock_mono().Inverse().rate());
      const float scale = DbToScale(state.gain_db);
      for (int64_t i = dest_frame_offset; i < dest_frame_count; ++i) {
        gain_scales_[i] *=
            scale +
            state.linear_scale_slope_per_ns *
                static_cast<float>(gain_control_ns_per_dest_frame.Scale(i - dest_frame_offset));
      }
    } else {
      // Gain is constant.
      gain_db += state.gain_db;
    }
  }

  if (dest_frame_offset == 0) {
    if (gain_db <= kMinGainDb) {
      gain_ = {.type = GainType::kSilent, .scale = 0.0f};
    } else if (is_ramping) {
      gain_ = {.type = GainType::kRamping, .scale_ramp = gain_scales_.data()};
    } else {
      gain_ = {.type = (gain_db == kUnityGainDb) ? GainType::kUnity : GainType::kNonUnity,
               .scale = DbToScale(gain_db)};
    }
    return;
  }

  const auto maybe_backfill_gain_scales_fn = [&]() {
    // TODO(fxbug.dev/114910): `GainType::kRamping` is misleading here, we should rename to reflect
    // this behavior where it only indicates that `Sampler::Gain::scale_ramp` should be used.
    if (gain_.type != GainType::kRamping) {
      // We lazily fill the previous frames only when needed.
      std::fill_n(gain_scales_.data(), dest_frame_offset, gain_.scale);
      gain_ = {.type = GainType::kRamping, .scale_ramp = gain_scales_.data()};
    }
  };

  if (is_ramping) {
    maybe_backfill_gain_scales_fn();
    return;
  }

  const float scale = DbToScale(gain_db);
  if (gain_.type == GainType::kRamping || scale != gain_.scale) {
    maybe_backfill_gain_scales_fn();
    std::fill(&gain_scales_[dest_frame_offset], &gain_scales_[dest_frame_count], scale);
  }
}

void MixerSource::SetDestGains(const std::unordered_set<GainControlId>& dest_gain_ids) {
  all_gain_ids_ = source_gain_ids_;
  all_gain_ids_.insert(dest_gain_ids.begin(), dest_gain_ids.end());
}

std::optional<PipelineStage::Packet> MixerSource::ReadNextSourcePacket(MixJobContext& ctx,
                                                                       int64_t dest_frame_count) {
  // Request enough source frames to produce `dest_frame_count` destination frames.
  const Fixed pos_filter_width = sampler_->pos_filter_length() - Fixed::FromRaw(1);
  const auto& state = sampler_->state();
  Fixed source_frame_count = state.SourceFromDestLength(dest_frame_count) + pos_filter_width;
  Fixed source_start_frame = state.next_source_frame();

  // Advance `source_start_frame` to our source's next available frame. This is needed because our
  // source's current position may be ahead of `state.next_source_frame` by up to `pos_filter_width`
  // frames. While we could keep track of this delta ourselves, it's easier to simply ask the source
  // for its current position.
  if (const auto next_readable_frame = source_->next_readable_frame();
      next_readable_frame && *next_readable_frame > source_start_frame) {
    const Fixed source_end_frame = source_start_frame + source_frame_count;
    source_start_frame = *next_readable_frame;
    source_frame_count = source_end_frame - source_start_frame;
    if (source_frame_count <= Fixed(0)) {
      // The source cannot be ahead of `state.next_source_frame` by more than `pos_filter_width`.
      FX_LOGS(WARNING) << ffl::String::DecRational << "Unexpectedly small source request"
                       << " [" << state.next_source_frame() << ", " << source_end_frame
                       << ") is entirely before next available frame " << (*next_readable_frame);
      return std::nullopt;
    }
  }

  // Round up so we always request an integral number of frames.
  return source_->Read(ctx, source_start_frame, source_frame_count.Ceiling());
}

// TODO(fxbug.dev/114393): Add more logging as needed from `Mixer::ReconcileClocksAndSetStepSize`.
void MixerSource::UpdateSamplerState(const TimelineFunction& dest_time_to_dest_frac_frame,
                                     int64_t dest_frame) {
  auto& state = sampler_->state();

  // Calculate the `TimelineRate` for `state.step_size`.
  const auto dest_frame_to_dest_time = DestTimeToDestFrame(dest_time_to_dest_frac_frame).Inverse();
  const TimelineRate source_frac_frames_per_dest_frame = TimelineRate::Product(
      dest_frame_to_dest_time.rate(), source_->presentation_time_to_frac_frame()->rate());

  const auto dest_frame_to_mono_time = dest_clock_->to_clock_mono() * dest_frame_to_dest_time;
  const auto mono_time_to_source_frac_frame =
      *source_->presentation_time_to_frac_frame() * source_clock_->to_clock_mono().Inverse();
  const auto dest_frame_to_source_frac_frame =
      mono_time_to_source_frac_frame * dest_frame_to_mono_time;

  const auto mono_time_for_dest = zx::time{dest_frame_to_mono_time.Apply(dest_frame)};

  if (last_source_time_to_source_frac_frame_ != source_->presentation_time_to_frac_frame()) {
    // If source timeline has been changed since the last mix call, reset the relationship between
    // the source and the destination streams.
    last_source_time_to_source_frac_frame_ = source_->presentation_time_to_frac_frame();
    state.ResetPositions(dest_frame, dest_frame_to_source_frac_frame);
    state.ResetSourceStride(source_frac_frames_per_dest_frame);
    clock_sync_->Reset(mono_time_for_dest);
    return;
  }

  if (dest_frame != state.next_dest_frame()) {
    // In most cases, we advance source position using `state.step_size`. For a destination
    // discontinuity of `N` frames, we update `state.next_dest_frame` by `N` and update
    // `state.next_source_frame` by `N * state.step_size`. However, if a discontinuity exceeds
    // `kMaxErrorThresholdDuration`, clocks have diverged to such an extent that we view the
    // discontinuity as unrecoverable, so we reset the relationship between the source and the
    // destination streams.
    const auto dest_gap_duration = zx::nsec(dest_frame_to_mono_time.rate().Scale(
        std::abs(dest_frame - state.next_dest_frame()), TimelineRate::RoundingMode::Ceiling));
    if (dest_gap_duration > kMaxErrorThresholdDuration) {
      state.ResetPositions(dest_frame, dest_frame_to_source_frac_frame);
      state.ResetSourceStride(source_frac_frames_per_dest_frame);
      clock_sync_->Reset(mono_time_for_dest);
      return;
    }
    state.AdvanceAllPositionsTo(dest_frame);
  }

  if (!clock_sync_->NeedsSynchronization()) {
    // Source and destination streams share the same clock, so no further synchronization is needed
    // besides the frame rate conversion.
    state.ResetSourceStride(source_frac_frames_per_dest_frame);
    return;
  }

  // Project `state.next_source_frame` (including `state.source_pos_modulo` effects) into system
  // monotonic time as `mono_time_for_source`. Record the difference (in nsecs) between
  // `mono_time_for_source` and `mono_time_for_dest` as the source position error.
  const auto mono_time_for_source = state.MonoTimeFromRunningSource(mono_time_to_source_frac_frame);
  state.set_source_pos_error(mono_time_for_source - mono_time_for_dest);

  // If source position error is less than 1 fractional source frame, we disregard it. This keeps us
  // from overreacting to precision-limit-related errors, when translated to higher-resolution
  // nsecs. Beyond 1 fractional frame though, we rate-adjust clocks using nsec precision.
  const zx::duration max_source_pos_error_to_not_tune =
      zx::nsec(mono_time_to_source_frac_frame.rate().Inverse().Scale(
          1, TimelineRate::RoundingMode::Ceiling));
  if (std::abs(state.source_pos_error().to_nsecs()) <=
      max_source_pos_error_to_not_tune.to_nsecs()) {
    state.set_source_pos_error(zx::nsec(0));
  }

  // If source error exceeds our threshold, allow a discontinuity, reset the relationship between
  // the source and the destination streams.
  if (std::abs(state.source_pos_error().get()) > kMaxErrorThresholdDuration.get()) {
    state.ResetPositions(dest_frame, dest_frame_to_source_frac_frame);
    state.ResetSourceStride(source_frac_frames_per_dest_frame);
    clock_sync_->Reset(mono_time_for_dest);
    return;
  }

  // Allow the clocks to self-synchronize to eliminate the position error.
  if (clock_sync_->follower()->koid() == source_clock_->koid()) {
    clock_sync_->Update(mono_time_for_dest, state.source_pos_error());
  } else {
    clock_sync_->Update(mono_time_for_dest, zx::nsec(0) - state.source_pos_error());
  }

  // In `WithMicroSRC` mode, we should apply a rate-conversion factor during SRC.
  if (clock_sync_->mode() == ClockSynchronizer::Mode::WithMicroSRC) {
    if (const auto micro_src_ppm = clock_sync_->follower_adjustment_ppm(); micro_src_ppm != 0) {
      const TimelineRate micro_src_factor{static_cast<uint64_t>(1'000'000 + micro_src_ppm),
                                          1'000'000};
      // We allow reduction for when the product exceeds a `uint64_t`-based ratio. Step size can be
      // approximate, as clocks (not SRC/step size) determine a stream absolute position, while SRC
      // just chases the position.
      state.ResetSourceStride(TimelineRate::Product(source_frac_frames_per_dest_frame,
                                                    micro_src_factor,
                                                    /*exact=*/false));
      return;
    }
  }

  state.ResetSourceStride(source_frac_frames_per_dest_frame);
}

}  // namespace media_audio
