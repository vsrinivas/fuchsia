// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_MIXER_SERVICE_MIX_PRODUCER_STAGE_H_
#define SRC_MEDIA_AUDIO_MIXER_SERVICE_MIX_PRODUCER_STAGE_H_

#include <lib/syslog/cpp/macros.h>

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

 protected:
  ProducerStage(std::string_view name, Format format,
                TimelineFunction ref_time_to_frac_presentation_frame)
      : PipelineStage(name, format),
        ref_time_to_frac_presentation_frame_(ref_time_to_frac_presentation_frame) {}

 private:
  TimelineFunction ref_time_to_frac_presentation_frame_;
};

}  // namespace media_audio_mixer_service

#endif  // SRC_MEDIA_AUDIO_MIXER_SERVICE_MIX_PRODUCER_STAGE_H_
