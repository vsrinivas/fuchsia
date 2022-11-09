// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_SERVICES_MIXER_MIX_RING_BUFFER_PRODUCER_STAGE_H_
#define SRC_MEDIA_AUDIO_SERVICES_MIXER_MIX_RING_BUFFER_PRODUCER_STAGE_H_

#include <lib/zx/time.h>

#include <functional>
#include <optional>
#include <utility>

#include "src/media/audio/lib/format2/fixed.h"
#include "src/media/audio/lib/format2/format.h"
#include "src/media/audio/services/mixer/common/memory_mapped_buffer.h"
#include "src/media/audio/services/mixer/mix/mix_job_context.h"
#include "src/media/audio/services/mixer/mix/pipeline_stage.h"
#include "src/media/audio/services/mixer/mix/ptr_decls.h"
#include "src/media/audio/services/mixer/mix/ring_buffer.h"

namespace media_audio {

// A ProducerStage driven by a ring buffer. This is a "simple" producer because it does not handle
// Start or Stop commands. This is intended to be embedded within a ProducerStage.
class SimpleRingBufferProducerStage : public PipelineStage {
 public:
  SimpleRingBufferProducerStage(std::string_view name, std::shared_ptr<RingBuffer> buffer,
                                PipelineThreadPtr initial_thread);

  // Implements `PipelineStage`.
  void AddSource(PipelineStagePtr source, AddSourceOptions options) final {
    UNREACHABLE << "SimpleRingBufferProducerStage should not have a source";
  }
  void RemoveSource(PipelineStagePtr source) final {
    UNREACHABLE << "SimpleRingBufferProducerStage should not have a source";
  }
  void UpdatePresentationTimeToFracFrame(std::optional<TimelineFunction> f) final;

 protected:
  // Implements `PipelineStage`.
  // Since there are no resources to release, advancing is a no-op.
  void AdvanceSelfImpl(Fixed frame) final {}
  void AdvanceSourcesImpl(MixJobContext& ctx, Fixed frame) final {}
  std::optional<Packet> ReadImpl(MixJobContext& ctx, Fixed start_frame, int64_t frame_count) final;

 private:
  const std::shared_ptr<RingBuffer> buffer_;
};

}  // namespace media_audio

#endif  // SRC_MEDIA_AUDIO_SERVICES_MIXER_MIX_RING_BUFFER_PRODUCER_STAGE_H_
