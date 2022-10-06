// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_SERVICES_MIXER_MIX_MIXER_SOURCE_H_
#define SRC_MEDIA_AUDIO_SERVICES_MIXER_MIX_MIXER_SOURCE_H_

#include <lib/syslog/cpp/macros.h>
#include <lib/zx/time.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <unordered_set>
#include <vector>

#include "src/media/audio/lib/clock/clock.h"
#include "src/media/audio/lib/clock/clock_synchronizer.h"
#include "src/media/audio/lib/format2/fixed.h"
#include "src/media/audio/lib/format2/format.h"
#include "src/media/audio/lib/processing/sampler.h"
#include "src/media/audio/services/common/logging.h"
#include "src/media/audio/services/mixer/common/basic_types.h"
#include "src/media/audio/services/mixer/mix/mix_job_context.h"
#include "src/media/audio/services/mixer/mix/mixer_gain_controls.h"
#include "src/media/audio/services/mixer/mix/pipeline_stage.h"
#include "src/media/audio/services/mixer/mix/silence_padding_stage.h"

namespace media_audio {

// Class that manages relevant information of a `MixerStage` source to mix onto destination stream.
//
// This consists of the computation of the combined gain to be applied into the source stream, as
// well as processing the samples in the source stream to mix onto the destination stream using an
// appropriate sampler implementation with respect to the synchronization needs between the source
// and destination streams.
//
// Combined gain computation is done by using the set of `GainControl`s that are connected to the
// source, combined with the `GainControl`s that are connected to the destination. The source gain
// controls are selected during the creation of this mixer source edge, and are accessed by the
// `options.gain_ids`. Similarly, the initial destination gain controls are passed via
// `dest_gain_ids` at construction to build the total set of gain controls to be combined. However,
// while the source gain controls are guaranteed to remain constant after the edge creation, the
// destination gain controls can be modified after the creation of this mixer source. Therefore, we
// keep track of the destination gain controls and update them via `SetDestGains` as requested.
//
// Once the combined gain is computed and updated via `PrepareSourceGainForNextMix` call,
// corresponding source samples can be processed and mixed onto the destination stream via `Advance`
// and `Mix` calls. These mainly follow the same `PipelineStage::Advance` and `PipelineStage::Read`
// call patterns, where `Read` roughly expands to a sequence of `PrepareSourceGainForNextMix` and
// `Mix` calls respectively.
class MixerSource {
 public:
  MixerSource(PipelineStagePtr source, PipelineStage::AddSourceOptions options,
              const std::unordered_set<GainControlId>& dest_gain_ids,
              int64_t max_dest_frame_count_per_mix);

  // Advances source to `dest_frame`.
  void Advance(MixJobContext& ctx, const TimelineFunction& dest_time_to_dest_frac_frame,
               Fixed dest_frame);

  // Mixes source onto destination with a given `dest_start_frame` and `dest_frame_count`, where
  // `dest_samples` points to the destination samples starting at `dest_start_frame`. If
  // `accumulate` is true, source samples will be accumulated into the existing `dest_samples`.
  // Otherwise, `dest_samples` will be filled directly by the corresponding source samples. Finally,
  // returns true if there were "potentially" non-silent frames that were mixed onto `dest_samples`.
  // Returns false otherwise, if no frames were mixed or all frames were guaranteed to be silent.
  bool Mix(MixJobContext& ctx, const TimelineFunction& dest_time_to_dest_frac_frame,
           Fixed dest_start_frame, int64_t dest_frame_count, float* dest_samples, bool accumulate);

  // Prepares combined source gain for the next `Mix` call for a given frame range of
  // `[dest_frame_offset, dest_frame_count)`, using the current state of `gain_controls`. Note that
  // combined source gain for the preceding frame range of `[0, dest_frame_offset)` must have
  // already been prepared by calling the function respectively prior to this call.
  void PrepareSourceGainForNextMix(MixJobContext& ctx, const MixerGainControls& gain_controls,
                                   const TimelineFunction& dest_time_to_dest_frac_frame,
                                   int64_t dest_frame_offset, int64_t dest_frame_count);

  // Sets the set of `dest_gain_ids` to be applied to the output edge.
  void SetDestGains(const std::unordered_set<GainControlId>& dest_gain_ids);

  // Returns the original source.
  const PipelineStagePtr& original_source() const { return source_->source(); }

  // Returns the set of all gains to be applied to this source. For debugging purposes only.
  const std::unordered_set<GainControlId>& all_gain_ids() const { return all_gain_ids_; }

  // Returns the most recently computed combined source gain to be used in the next `Mix` call. This
  // is prepared at the beginning of each `PipelineStage::Read` call in the destination stream by a
  // set of `PrepareSourceGainForNextMix` calls. For debugging purposes only.
  Sampler::Gain gain() const { return gain_; }

 private:
  // Reads next source packet of `dest_frame_count`, or `std::nullopt` if no available packet.
  std::optional<PipelineStage::Packet> ReadNextSourcePacket(MixJobContext& ctx,
                                                            int64_t dest_frame_count);

  // Updates the sampler's internal state with the given `dest_frame`, which includes the clock
  // reconciliation to compute the rate adjustments and the long-running positions respectively.
  void UpdateSamplerState(const TimelineFunction& dest_time_to_dest_frac_frame, int64_t dest_frame);

  std::shared_ptr<ClockSynchronizer> clock_sync_;
  std::optional<TimelineFunction> last_source_time_to_source_frac_frame_ = std::nullopt;
  // TODO(fxbug.dev/87651): This is a workaround to make sure `Advance` and `Mix` calls have the
  // most up-to-date clock states after reconciliation. Remove these `Clock` dependencies, and use
  // the corresponding `ClockSnapshot`s via `MixJobContext::clocks` instead.
  std::shared_ptr<Clock> dest_clock_;
  std::shared_ptr<Clock> source_clock_;

  std::shared_ptr<Sampler> sampler_;
  std::unique_ptr<SilencePaddingStage> source_;

  std::unordered_set<GainControlId> source_gain_ids_;
  std::unordered_set<GainControlId> all_gain_ids_;
  Sampler::Gain gain_;
  std::vector<float> gain_scales_;
  std::optional<int64_t> last_prepared_gain_frame_ = std::nullopt;
};

}  // namespace media_audio

#endif  // SRC_MEDIA_AUDIO_SERVICES_MIXER_MIX_MIXER_SOURCE_H_
