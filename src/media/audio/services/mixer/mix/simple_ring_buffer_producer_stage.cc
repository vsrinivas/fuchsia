// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/services/mixer/mix/simple_ring_buffer_producer_stage.h"

#include <lib/syslog/cpp/macros.h>
#include <lib/zx/vmo.h>

#include <algorithm>
#include <optional>

#include "src/media/audio/lib/format2/fixed.h"
#include "src/media/audio/services/common/logging.h"
#include "src/media/audio/services/mixer/mix/mix_job_context.h"
#include "src/media/audio/services/mixer/mix/pipeline_stage.h"

namespace media_audio {

SimpleRingBufferProducerStage::SimpleRingBufferProducerStage(std::string_view name,
                                                             std::shared_ptr<RingBuffer> buffer)
    : PipelineStage(name, buffer->format(), buffer->reference_clock()),
      buffer_(std::move(buffer)) {}

void SimpleRingBufferProducerStage::UpdatePresentationTimeToFracFrame(
    std::optional<TimelineFunction> f) {
  set_presentation_time_to_frac_frame(f);
}

std::optional<PipelineStage::Packet> SimpleRingBufferProducerStage::ReadImpl(MixJobContext& ctx,
                                                                             Fixed start_frame,
                                                                             int64_t frame_count) {
  // We don't need to cache the returned packet since we don't generate any data dynamically.
  auto packet = buffer_->Read(start_frame.Floor(), frame_count);
  return MakeUncachedPacket(packet.start_frame(), packet.frame_count(), packet.payload());
}

}  // namespace media_audio
