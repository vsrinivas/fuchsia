// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/tap_stage.h"

#include <trace/event.h>

#include "src/lib/syslog/cpp/logger.h"

namespace media::audio {

TapStage::TapStage(std::shared_ptr<Stream> source, std::shared_ptr<Stream> tap)
    : Stream(source->format()), source_(std::move(source)), tap_(std::move(tap)) {
  FX_DCHECK(source_->format() == tap_->format());
}

std::optional<Stream::Buffer> TapStage::LockBuffer(zx::time ref_time, int64_t frame,
                                                   uint32_t frame_count) {
  TRACE_DURATION("audio", "TapStage::LockBuffer", "frame", frame, "length", frame_count);
  auto input_buffer = source_->LockBuffer(ref_time, frame, frame_count);
  if (!input_buffer) {
    return std::nullopt;
  }

  uint8_t* input_ptr = reinterpret_cast<uint8_t*>(input_buffer->payload());
  int64_t output_buffer_frame = input_buffer->start().Floor();
  uint32_t output_frames_outstanding = input_buffer->length().Floor();

  while (output_frames_outstanding > 0) {
    auto output_buffer = tap_->LockBuffer(ref_time, output_buffer_frame, output_frames_outstanding);
    if (!output_buffer) {
      break;
    }

    uint32_t frames_copied = std::min(output_buffer->length().Floor(), output_frames_outstanding);
    uint32_t bytes_copied = frames_copied * format().bytes_per_frame();
    memcpy(output_buffer->payload(), input_ptr, bytes_copied);

    input_ptr += bytes_copied;
    output_buffer_frame += frames_copied;
    output_frames_outstanding -= frames_copied;
  }

  return input_buffer;
}

}  // namespace media::audio
