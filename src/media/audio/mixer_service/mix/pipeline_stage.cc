// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/mixer_service/mix/pipeline_stage.h"

#include <optional>

#include "src/media/audio/mixer_service/common/basic_types.h"
#include "src/media/audio/mixer_service/mix/packet.h"

namespace media_audio_mixer_service {

void PipelineStage::Advance(Fixed frame) {
  // TODO(fxbug.dev/87651): Add more logging and tracing etc (similar to `ReadableStream`).
  FX_CHECK(!is_locked_);

  // Advance the next read frame.
  if (next_read_frame_ && frame <= *next_read_frame_) {
    // Next read frame is already passed the advanced point.
    return;
  }
  next_read_frame_ = frame;

  if (cached_buffer_ && frame < cached_buffer_->end()) {
    // Cached buffer is still in use.
    return;
  }
  cached_buffer_ = std::nullopt;
  AdvanceImpl(frame);
}

std::optional<PipelineStage::Buffer> PipelineStage::Read(Fixed start_frame, int64_t frame_count) {
  // TODO(fxbug.dev/87651): Add more logging and tracing etc (similar to `ReadableStream`).
  FX_CHECK(!is_locked_);

  // Once a frame has been consumed, it cannot be locked again, we cannot travel backwards in time.
  FX_CHECK(!next_read_frame_ || start_frame >= *next_read_frame_);

  // Check if we can reuse the cached buffer.
  if (auto out_buffer = ReadFromCachedBuffer(start_frame, frame_count)) {
    return out_buffer;
  }
  cached_buffer_ = std::nullopt;

  auto buffer = ReadImpl(start_frame, frame_count);
  if (!buffer) {
    Advance(start_frame + Fixed(frame_count));
    return std::nullopt;
  }
  FX_CHECK(buffer->length() > 0);

  is_locked_ = true;
  if (!buffer->is_cached_) {
    return buffer;
  }

  cached_buffer_ = std::move(buffer);
  auto out_buffer = ReadFromCachedBuffer(start_frame, frame_count);
  FX_CHECK(out_buffer);
  return out_buffer;
}

PipelineStage::Buffer PipelineStage::MakeCachedBuffer(Fixed start_frame, int64_t frame_count,
                                                      void* payload) {
  // This buffer will be stored in `cached_buffer_`. It won't be returned to the `Read` caller,
  // instead we'll use `ReadFromCachedBuffer` to return a proxy to this buffer.
  return Buffer({format_, start_frame, frame_count, payload}, /*is_cached=*/true,
                /*destructor=*/nullptr);
}

PipelineStage::Buffer PipelineStage::MakeUncachedBuffer(Fixed start_frame, int64_t frame_count,
                                                        void* payload) {
  return Buffer({format_, start_frame, frame_count, payload}, /*is_cached=*/false,
                [this, start_frame](int64_t frames_consumed) {
                  // Unlock the stream.
                  is_locked_ = false;
                  Advance(start_frame + Fixed(frames_consumed));
                });
}

std::optional<PipelineStage::Buffer> PipelineStage::ForwardBuffer(
    std::optional<Buffer>&& buffer, std::optional<Fixed> start_frame) {
  if (!buffer) {
    return std::nullopt;
  }
  const auto buffer_start = start_frame ? *start_frame : buffer->start();
  return Buffer(
      // Wrap the buffer with a proxy so we can be notified when the buffer is unlocked.
      {buffer->format(), buffer_start, buffer->length(), buffer->payload()},
      /*is_cached=*/false,
      [this, buffer_start, buffer = std::move(buffer)](int64_t frames_consumed) mutable {
        // Unlock the stream.
        is_locked_ = false;
        // What is consumed from the proxy is also consumed from the source buffer.
        buffer->set_frames_consumed(frames_consumed);
        // Destroy the source buffer before calling `Advance` to ensure the source stream is
        // unlocked before it is advanced.
        buffer = std::nullopt;
        Advance(buffer_start + Fixed(frames_consumed));
      });
}

std::optional<PipelineStage::Buffer> PipelineStage::ReadFromCachedBuffer(Fixed start_frame,
                                                                         int64_t frame_count) {
  if (cached_buffer_) {
    if (auto intersect = cached_buffer_->IntersectionWith(start_frame, frame_count)) {
      return MakeUncachedBuffer(intersect->start(), intersect->length(), intersect->payload());
    }
  }
  return std::nullopt;
}

}  // namespace media_audio_mixer_service
