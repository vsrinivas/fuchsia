// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_SERVICES_MIXER_MIX_SPLITTER_PRODUCER_STAGE_H_
#define SRC_MEDIA_AUDIO_SERVICES_MIXER_MIX_SPLITTER_PRODUCER_STAGE_H_

#include <lib/zx/time.h>

#include <string>

#include "src/media/audio/lib/format2/fixed.h"
#include "src/media/audio/lib/format2/format.h"
#include "src/media/audio/services/mixer/mix/mix_job_context.h"
#include "src/media/audio/services/mixer/mix/packet_view.h"
#include "src/media/audio/services/mixer/mix/pipeline_stage.h"
#include "src/media/audio/services/mixer/mix/ptr_decls.h"
#include "src/media/audio/services/mixer/mix/ring_buffer.h"
#include "src/media/audio/services/mixer/mix/splitter_consumer_stage.h"

namespace media_audio {

// Represents a destination stream of a splitter. See ../docs/splitters.md.
class SplitterProducerStage : public PipelineStage {
 public:
  struct Args {
    // Name of this stage.
    std::string_view name;

    // Format of audio produced by this stage.
    Format format;

    // Reference clock used by this splitter.
    UnreadableClock reference_clock;

    // The splitter's buffer.
    std::shared_ptr<RingBuffer> ring_buffer;

    // Represents the splitter's source stream.
    std::shared_ptr<SplitterConsumerStage> consumer;
  };

  explicit SplitterProducerStage(Args args);

  // Implements `PipelineStage`.
  void AddSource(PipelineStagePtr source, AddSourceOptions options) final {
    UNREACHABLE << "Producers should not have a source";
  }
  void RemoveSource(PipelineStagePtr source) final {
    UNREACHABLE << "Producers should not have a source";
  }
  void UpdatePresentationTimeToFracFrame(std::optional<TimelineFunction> f) final;

 protected:
  // Implements `PipelineStage`.
  void AdvanceSelfImpl(Fixed frame) final {}  // no-op since the backing buffer is a ring buffer
  void AdvanceSourcesImpl(MixJobContext& ctx, Fixed frame) final;
  std::optional<Packet> ReadImpl(MixJobContext& ctx, Fixed start_frame, int64_t frame_count) final;

 private:
  void RecomputeConsumerFrameOffset();

  std::shared_ptr<RingBuffer> ring_buffer_;
  std::shared_ptr<SplitterConsumerStage> consumer_;

  // Given a frame on our frame timeline, we can compute the equivalent frame on the consumer's
  // frame timeline using the formula `f_consumer = f_producer + consumer_frame_offset_`. This is
  // `std::nullopt` iff either the downstream or internal frame timeline is stopped.
  std::optional<Fixed> consumer_frame_offset_;
};

}  // namespace media_audio

#endif  // SRC_MEDIA_AUDIO_SERVICES_MIXER_MIX_SPLITTER_PRODUCER_STAGE_H_
