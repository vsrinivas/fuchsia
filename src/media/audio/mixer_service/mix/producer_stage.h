// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_MIXER_SERVICE_MIX_PRODUCER_STAGE_H_
#define SRC_MEDIA_AUDIO_MIXER_SERVICE_MIX_PRODUCER_STAGE_H_

#include <lib/syslog/cpp/macros.h>

#include <memory>
#include <string_view>
#include <utility>

#include "src/media/audio/mixer_service/common/basic_types.h"
#include "src/media/audio/mixer_service/mix/pipeline_stage.h"
#include "src/media/audio/mixer_service/mix/ptr_decls.h"

namespace media_audio_mixer_service {

// Producer stage that does not have any inputs and produces a single output.
class ProducerStage : public PipelineStage {
 public:
  // Implements `PipelineStage`.
  void AddSource(PipelineStagePtr src) final {
    FX_CHECK(false) << "ProducerStage should not have input sources";
  }
  void RemoveSource(PipelineStagePtr src) final {
    FX_CHECK(false) << "ProducerStage should not have input sources";
  }
  TimelineFunction ref_time_to_frac_presentation_frame() const final {
    return ref_time_to_frac_presentation_frame_;
  }
  AudioClock& reference_clock() final { return *audio_clock_; }

 protected:
  ProducerStage(std::string_view name, Format format, std::unique_ptr<AudioClock> audio_clock,
                TimelineFunction ref_time_to_frac_presentation_frame)
      : PipelineStage(name, format),
        audio_clock_(std::move(audio_clock)),
        ref_time_to_frac_presentation_frame_(ref_time_to_frac_presentation_frame) {
    FX_CHECK(audio_clock_);
  }

 private:
  std::unique_ptr<AudioClock> audio_clock_;
  TimelineFunction ref_time_to_frac_presentation_frame_;
};

}  // namespace media_audio_mixer_service

#endif  // SRC_MEDIA_AUDIO_MIXER_SERVICE_MIX_PRODUCER_STAGE_H_
