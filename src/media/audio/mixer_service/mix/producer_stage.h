// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_MIXER_SERVICE_MIX_PRODUCER_STAGE_H_
#define SRC_MEDIA_AUDIO_MIXER_SERVICE_MIX_PRODUCER_STAGE_H_

#include <lib/syslog/cpp/macros.h>

#include <string_view>

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

 protected:
  ProducerStage(std::string_view name, Format format, zx_koid_t reference_clock_koid)
      : PipelineStage(name, format, reference_clock_koid) {}
};

}  // namespace media_audio_mixer_service

#endif  // SRC_MEDIA_AUDIO_MIXER_SERVICE_MIX_PRODUCER_STAGE_H_
