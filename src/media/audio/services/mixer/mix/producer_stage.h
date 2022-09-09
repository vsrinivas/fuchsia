// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_SERVICES_MIXER_MIX_PRODUCER_STAGE_H_
#define SRC_MEDIA_AUDIO_SERVICES_MIXER_MIX_PRODUCER_STAGE_H_

#include <lib/syslog/cpp/macros.h>

#include <string_view>
#include <unordered_set>

#include "src/media/audio/lib/format2/format.h"
#include "src/media/audio/services/common/logging.h"
#include "src/media/audio/services/mixer/common/basic_types.h"
#include "src/media/audio/services/mixer/mix/pipeline_stage.h"
#include "src/media/audio/services/mixer/mix/ptr_decls.h"

namespace media_audio {

// A Producer has zero source streams and a single destination stream.
class ProducerStage : public PipelineStage {
 public:
  // Implements `PipelineStage`.
  void AddSource(PipelineStagePtr source, AddSourceOptions options) final {
    UNREACHABLE << "ProducerStage should not have a source";
  }
  void RemoveSource(PipelineStagePtr source) final {
    UNREACHABLE << "ProducerStage should not have a source";
  }
  void UpdatePresentationTimeToFracFrame(std::optional<TimelineFunction> f) override;

 protected:
  ProducerStage(std::string_view name, Format format, zx_koid_t reference_clock_koid)
      : PipelineStage(name, format, reference_clock_koid) {}

  void AdvanceSourcesImpl(MixJobContext& ctx, Fixed frame) final {}
};

}  // namespace media_audio

#endif  // SRC_MEDIA_AUDIO_SERVICES_MIXER_MIX_PRODUCER_STAGE_H_
