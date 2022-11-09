// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_SERVICES_MIXER_MIX_MIXER_STAGE_H_
#define SRC_MEDIA_AUDIO_SERVICES_MIXER_MIX_MIXER_STAGE_H_

#include <lib/zx/time.h>
#include <zircon/types.h>

#include <cstdint>
#include <optional>
#include <string_view>
#include <unordered_set>
#include <vector>

#include "src/media/audio/services/mixer/common/basic_types.h"
#include "src/media/audio/services/mixer/mix/mix_job_context.h"
#include "src/media/audio/services/mixer/mix/mixer_gain_controls.h"
#include "src/media/audio/services/mixer/mix/mixer_source.h"
#include "src/media/audio/services/mixer/mix/pipeline_stage.h"
#include "src/media/audio/services/mixer/mix/ptr_decls.h"

namespace media_audio {

// Stage that mixes multiple source streams into a single destination stream.
class MixerStage : public PipelineStage {
 public:
  MixerStage(std::string_view name, Format format, UnreadableClock reference_clock,
             PipelineThreadPtr initial_thread, int64_t max_dest_frame_count_per_mix);

  // Implements `PipelineStage`.
  void AddSource(PipelineStagePtr source, AddSourceOptions options) final;
  void RemoveSource(PipelineStagePtr source) final;
  void UpdatePresentationTimeToFracFrame(std::optional<TimelineFunction> f) final;

  // Sets the set of `gain_ids` to be applied to the destination stream.
  void SetDestGains(std::unordered_set<GainControlId> gain_ids);

  // Returns the mixer gain controls.
  MixerGainControls& gain_controls() { return gain_controls_; }

 protected:
  void AdvanceSelfImpl(Fixed frame) final {}
  void AdvanceSourcesImpl(MixJobContext& ctx, Fixed frame) final;
  std::optional<Packet> ReadImpl(MixJobContext& ctx, Fixed start_frame, int64_t frame_count) final;

 private:
  // Prepares source gains for a given integral `start_frame` and `frame_count`.
  void PrepareSourceGains(MixJobContext& ctx, Fixed start_frame, int64_t frame_count);

  // Pre-allocated destination buffer in float-32 format with a `max_dest_frame_count_per_mix_`.
  const int64_t max_dest_frame_count_per_mix_;
  std::vector<float> dest_buffer_;

  MixerGainControls gain_controls_;
  std::unordered_set<GainControlId> dest_gain_ids_;

  std::vector<MixerSource> sources_;
};

}  // namespace media_audio

#endif  // SRC_MEDIA_AUDIO_SERVICES_MIXER_MIX_MIXER_STAGE_H_
