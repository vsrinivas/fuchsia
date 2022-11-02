// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_SERVICES_MIXER_MIX_SPLITTER_CONSUMER_STAGE_H_
#define SRC_MEDIA_AUDIO_SERVICES_MIXER_MIX_SPLITTER_CONSUMER_STAGE_H_

#include <lib/zx/time.h>

#include <atomic>
#include <limits>
#include <string>

#include "src/media/audio/lib/format2/fixed.h"
#include "src/media/audio/lib/format2/format.h"
#include "src/media/audio/services/mixer/mix/base_consumer_stage.h"
#include "src/media/audio/services/mixer/mix/mix_job_context.h"
#include "src/media/audio/services/mixer/mix/ptr_decls.h"
#include "src/media/audio/services/mixer/mix/ring_buffer.h"

namespace media_audio {

// Represents the source stream of a splitter. See ../docs/splitters.md.
//
// Unlike other PipelineStages, a few methods can be accessed from other threads. These methods are
// intended to be called from a SplitterProducerStage.
class SplitterConsumerStage : public BaseConsumerStage {
 public:
  struct Args {
    // Name of this stage.
    std::string_view name;

    // Format of audio consumed by this stage.
    Format format;

    // Reference clock used by this splitter.
    UnreadableClock reference_clock;

    // The splitter's buffer.
    std::shared_ptr<RingBuffer> ring_buffer;
  };

  explicit SplitterConsumerStage(Args args);

  // Implements `PipelineStage`.
  // TODO(fxbug.dev/87651): add TA_REQ(thread()->checker()) to this declaration in PipelineStage
  void UpdatePresentationTimeToFracFrame(std::optional<TimelineFunction> f) final;

  // Fills the ring buffer up to what is needed for the given mix job.
  void FillBuffer(MixJobContext& ctx) TA_REQ(thread()->checker());

  // Advance this splitter's source stream.
  void AdvanceSource(MixJobContext& ctx, Fixed frame) TA_REQ(thread()->checker());

  // Sets the maximum delay on any output pipeline downstream of this splitter.
  void set_max_downstream_output_pipeline_delay(zx::duration delay) TA_REQ(thread()->checker());

  // Returns the maximum delay on any output pipeline downstream of this splitter.
  zx::duration max_downstream_output_pipeline_delay() const TA_REQ(thread()->checker());

  // Equivalent to PipelineStage::presentation_time_to_frac_frame, but may be called from
  // SplitterProducerStage, which may be running on a different thread. This is initially
  // std::nullopt (stopped), then eventually changes to non-nullopt (started), after which point it
  // never changes. Hence, if the producer sees a non-nullopt value, it should not be concerned
  // about concurrent changes.
  //
  // TODO(fxbug.dev/87651): add TA_REQ(thread()->checker()) to
  // PipelineStage::presentation_time_to_frac_frame
  [[nodiscard]] std::optional<TimelineFunction> presentation_time_to_frac_frame() const;

  // Returns one frame after the last frame written, or INT64_MIN if no frames have been written.
  // May be called from SplitterProducerStage.
  [[nodiscard]] int64_t end_of_last_fill() const { return end_of_last_fill_.load(); }

 private:
  std::optional<zx::duration> max_downstream_output_pipeline_delay_ TA_GUARDED(thread()->checker());

  std::atomic<int64_t> end_of_last_fill_{std::numeric_limits<int64_t>::min()};
  std::atomic<bool> have_presentation_time_to_frac_frame_{false};
};

}  // namespace media_audio

#endif  // SRC_MEDIA_AUDIO_SERVICES_MIXER_MIX_SPLITTER_CONSUMER_STAGE_H_
