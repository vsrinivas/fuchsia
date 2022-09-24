// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/services/mixer/mix/mixer_stage.h"

#include <lib/syslog/cpp/macros.h>
#include <lib/zx/time.h>
#include <zircon/types.h>

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string_view>
#include <unordered_set>
#include <utility>

#include "fidl/fuchsia.mediastreams/cpp/wire_types.h"
#include "src/media/audio/lib/clock/clock_snapshot.h"
#include "src/media/audio/lib/format2/fixed.h"
#include "src/media/audio/lib/format2/format.h"
#include "src/media/audio/lib/processing/gain.h"
#include "src/media/audio/services/mixer/common/basic_types.h"
#include "src/media/audio/services/mixer/mix/mix_job_context.h"
#include "src/media/audio/services/mixer/mix/pipeline_stage.h"
#include "src/media/audio/services/mixer/mix/ptr_decls.h"

namespace media_audio {

using ::fuchsia_audio::SampleType;

MixerStage::MixerStage(std::string_view name, Format format, UnreadableClock reference_clock,
                       int64_t max_dest_frame_count_per_mix)
    : PipelineStage(name, format, std::move(reference_clock)),
      max_dest_frame_count_per_mix_(max_dest_frame_count_per_mix),
      dest_buffer_(max_dest_frame_count_per_mix_ * format.channels(), 0.0f) {
  FX_CHECK(format.sample_type() == SampleType::kFloat32);
  FX_CHECK(max_dest_frame_count_per_mix_ > 0);
}

void MixerStage::AddSource(PipelineStagePtr source, AddSourceOptions options) {
  if (!source) {
    FX_LOGS(ERROR) << "Cannot add null source";
    return;
  }
  FX_CHECK(std::find_if(sources_.begin(), sources_.end(),
                        [&source](const MixerSource& mixer_source) {
                          return mixer_source.original_source() == source;
                        }) == sources_.end())
      << "source " << source->name() << " already exists";

  sources_.emplace_back(std::move(source), std::move(options), dest_gain_ids_,
                        max_dest_frame_count_per_mix_);
}

void MixerStage::RemoveSource(PipelineStagePtr source) {
  if (!source) {
    FX_LOGS(ERROR) << "Cannot remove null source";
    return;
  }
  const auto it =
      std::find_if(sources_.begin(), sources_.end(), [&source](const MixerSource& mixer_source) {
        return mixer_source.original_source() == source;
      });
  FX_CHECK(it != sources_.end()) << "source " << source->name() << " not found";

  sources_.erase(it);
}

void MixerStage::UpdatePresentationTimeToFracFrame(std::optional<TimelineFunction> f) {
  set_presentation_time_to_frac_frame(f);
}

void MixerStage::SetDestGains(std::unordered_set<GainControlId> gain_ids) {
  dest_gain_ids_ = std::move(gain_ids);
  for (auto& source : sources_) {
    source.SetDestGains(dest_gain_ids_);
  }
}

void MixerStage::AdvanceSourcesImpl(MixJobContext& ctx, Fixed frame) {
  // `MixerStage` always produces data on integrally-aligned frames.
  frame = Fixed(frame.Floor());

  const auto dest_clock = ctx.clocks().SnapshotFor(reference_clock());
  const auto dest_time = PresentationTimeFromFrame(frame);
  const auto mono_time = dest_clock.MonotonicTimeFromReferenceTime(dest_time);
  gain_controls_.Advance(ctx.clocks(), mono_time);

  for (auto& source : sources_) {
    source.Advance(ctx, *presentation_time_to_frac_frame(), frame);
  }
}

std::optional<PipelineStage::Packet> MixerStage::ReadImpl(MixJobContext& ctx, Fixed start_frame,
                                                          int64_t frame_count) {
  // `MixerStage` always produces data on integrally-aligned frames.
  start_frame = Fixed(start_frame.Floor());

  while (frame_count > 0) {
    const int64_t current_frame_count = std::min(frame_count, max_dest_frame_count_per_mix_);
    std::fill_n(dest_buffer_.begin(), format().channels() * current_frame_count, 0.0f);

    PrepareSourceGains(ctx, start_frame, current_frame_count);

    bool has_potentially_nonsilent_frames = false;
    for (auto& source : sources_) {
      has_potentially_nonsilent_frames |=
          source.Mix(ctx, *presentation_time_to_frac_frame(), start_frame, current_frame_count,
                     dest_buffer_.data(), /*accumulate=*/has_potentially_nonsilent_frames);
    }
    if (has_potentially_nonsilent_frames) {
      return MakeCachedPacket(start_frame, current_frame_count, dest_buffer_.data());
    }

    // No "potentially" non-silent frames were mixed so far, advance frames.
    start_frame += Fixed(current_frame_count);
    frame_count -= current_frame_count;
  }

  // No frames left to mix.
  return std::nullopt;
}

void MixerStage::PrepareSourceGains(MixJobContext& ctx, Fixed start_frame, int64_t frame_count) {
  const auto& clocks = ctx.clocks();
  const auto dest_clock = clocks.SnapshotFor(reference_clock());

  // TODO(fxbug.dev/87651): This is actually only needed if a new source, with new set of gain
  // controls which did not already exist in `gain_controls_`, is added to the mixer. Otherwise,
  // since `ReadImpl` is never called without advancing to `start_frame`, this is redundant.
  const auto start_mono_time =
      dest_clock.MonotonicTimeFromReferenceTime(PresentationTimeFromFrame(start_frame));
  gain_controls_.Advance(clocks, start_mono_time);

  const auto end_mono_time =
      start_mono_time + zx::nsec(format().frames_per_ns().Inverse().Scale(frame_count));
  int64_t frame_offset = 0;
  while (frame_offset < frame_count) {
    const auto next_mono_time = gain_controls_.NextScheduledStateChange(clocks);
    if (!next_mono_time.has_value() || next_mono_time >= end_mono_time) {
      // No more state changes in the range, prepare the remaining buffer.
      for (auto& source : sources_) {
        source.PrepareSourceGainForNextMix(ctx, gain_controls_, *presentation_time_to_frac_frame(),
                                           frame_offset, frame_count);
      }
      gain_controls_.Advance(clocks, end_mono_time);
      return;
    }

    const Fixed next_frame =
        FrameFromPresentationTime(dest_clock.ReferenceTimeFromMonotonicTime(*next_mono_time));
    const int64_t next_frame_offset = Fixed(next_frame - start_frame).Floor();
    for (auto& source : sources_) {
      source.PrepareSourceGainForNextMix(ctx, gain_controls_, *presentation_time_to_frac_frame(),
                                         frame_offset, next_frame_offset);
    }
    frame_offset = next_frame_offset;

    gain_controls_.Advance(clocks, *next_mono_time);
  }
}

}  // namespace media_audio
