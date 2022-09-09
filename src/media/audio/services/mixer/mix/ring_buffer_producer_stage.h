// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_SERVICES_MIXER_MIX_RING_BUFFER_PRODUCER_STAGE_H_
#define SRC_MEDIA_AUDIO_SERVICES_MIXER_MIX_RING_BUFFER_PRODUCER_STAGE_H_

#include <lib/fzl/vmo-mapper.h>
#include <lib/zx/time.h>

#include <functional>
#include <optional>
#include <utility>

#include "src/media/audio/lib/format2/fixed.h"
#include "src/media/audio/lib/format2/format.h"
#include "src/media/audio/services/mixer/mix/mix_job_context.h"
#include "src/media/audio/services/mixer/mix/producer_stage.h"

namespace media_audio {

class RingBufferProducerStage : public ProducerStage {
 public:
  // A function that returns the safe read frame for the current time.
  // TODO(fxbug.dev/87651): Move this out to a common `ring_buffer` file as `SafeReadWriteFn`?
  using SafeReadFrameFn = std::function<int64_t()>;

  RingBufferProducerStage(Format format, zx_koid_t reference_clock_koid, fzl::VmoMapper vmo_mapper,
                          int64_t frame_count, SafeReadFrameFn safe_read_frame_fn);

  // Returns the ring buffer's size in frames.
  int64_t frame_count() const { return frame_count_; }

 protected:
  // Since there are no resources to release, advancing is a no-op.
  void AdvanceSelfImpl(Fixed frame) final {}

  // Implements `PipelineStage`.
  std::optional<Packet> ReadImpl(MixJobContext& ctx, Fixed start_frame, int64_t frame_count) final;

 private:
  fzl::VmoMapper vmo_mapper_;
  int64_t frame_count_ = 0;
  SafeReadFrameFn safe_read_frame_fn_;
};

}  // namespace media_audio

#endif  // SRC_MEDIA_AUDIO_SERVICES_MIXER_MIX_RING_BUFFER_PRODUCER_STAGE_H_
