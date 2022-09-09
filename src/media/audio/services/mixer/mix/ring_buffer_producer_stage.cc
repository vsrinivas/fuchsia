// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/services/mixer/mix/ring_buffer_producer_stage.h"

#include <lib/syslog/cpp/macros.h>
#include <lib/zx/vmo.h>

#include <algorithm>
#include <optional>

#include "src/media/audio/lib/format2/fixed.h"
#include "src/media/audio/services/common/logging.h"
#include "src/media/audio/services/mixer/mix/mix_job_context.h"
#include "src/media/audio/services/mixer/mix/pipeline_stage.h"

namespace media_audio {

RingBufferProducerStage::RingBufferProducerStage(Format format, zx_koid_t reference_clock_koid,
                                                 fzl::VmoMapper vmo_mapper, int64_t frame_count,
                                                 SafeReadFrameFn safe_read_frame_fn)
    : ProducerStage("RingBufferProducerStage", format, reference_clock_koid),
      vmo_mapper_(std::move(vmo_mapper)),
      frame_count_(frame_count),
      safe_read_frame_fn_(std::move(safe_read_frame_fn)) {
  FX_CHECK(vmo_mapper_.start());
  FX_CHECK(vmo_mapper_.size() >= static_cast<uint64_t>(format.bytes_per_frame() * frame_count_));
  FX_CHECK(safe_read_frame_fn_);
}

std::optional<PipelineStage::Packet> RingBufferProducerStage::ReadImpl(MixJobContext& ctx,
                                                                       Fixed start_frame,
                                                                       int64_t frame_count) {
  const int64_t requested_start_frame = start_frame.Floor();
  const int64_t requested_end_frame = requested_start_frame + frame_count;

  const int64_t valid_end_frame = safe_read_frame_fn_() + 1;
  const int64_t valid_start_frame = valid_end_frame - frame_count_;
  if (requested_start_frame >= valid_end_frame || requested_end_frame <= valid_start_frame) {
    return std::nullopt;
  }

  // Calculate "absolute" frames before the ring size adjustment.
  const int64_t absolute_start_frame = std::max(requested_start_frame, valid_start_frame);
  const int64_t absolute_end_frame = std::min(requested_end_frame, valid_end_frame);

  // Wrap the absolute frames around the ring to calculate the "relative" frames to be returned.
  int64_t relative_start_frame = absolute_start_frame % frame_count_;
  if (relative_start_frame < 0) {
    relative_start_frame += frame_count_;
  }
  int64_t relative_end_frame = absolute_end_frame % frame_count_;
  if (relative_end_frame < 0) {
    relative_end_frame += frame_count_;
  }
  if (relative_end_frame <= relative_start_frame) {
    relative_end_frame = frame_count_;
  }

  const int64_t packet_frame_count = relative_end_frame - relative_start_frame;
  void* packet_payload = reinterpret_cast<uint8_t*>(vmo_mapper_.start()) +
                         relative_start_frame * format().bytes_per_frame();

  // Ring buffers are synchronized only by time, which means there may not be a synchronization
  // happens-before edge connecting the last writer with the current reader, which means we must
  // invalidate our cache to ensure we read the latest data.
  //
  // This is especially important when the ring buffer represents a buffer shared with HW, because
  // the last write may have happened very recently, increasing the likelihood that our local cache
  // is out-of-date. This is less important when the buffer is used in SW only because it is more
  // likely that the last write happened long enough ago that our cache has been flushed in the
  // interim, however to be strictly correct, a flush is needed in all cases.
  const int64_t payload_size = packet_frame_count * format().bytes_per_frame();
  zx_cache_flush(packet_payload, payload_size, ZX_CACHE_FLUSH_DATA | ZX_CACHE_FLUSH_INVALIDATE);

  // We don't need to cache the returned packet, since we don't generate any data dynamically.
  return MakeUncachedPacket(Fixed(absolute_start_frame), packet_frame_count, packet_payload);
}

}  // namespace media_audio
