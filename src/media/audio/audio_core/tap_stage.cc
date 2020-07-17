// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/tap_stage.h"

#include <lib/syslog/cpp/macros.h>
#include <lib/trace/event.h>

namespace media::audio {

TapStage::TapStage(std::shared_ptr<ReadableStream> source, std::shared_ptr<WritableStream> tap)
    : ReadableStream(source->format()), source_(std::move(source)), tap_(std::move(tap)) {
  FX_DCHECK(source_->format() == tap_->format());
  FX_DCHECK(source_->reference_clock() == tap_->reference_clock());
}

std::optional<ReadableStream::Buffer> TapStage::ReadLock(zx::time dest_ref_time, int64_t frame,
                                                         uint32_t frame_count) {
  TRACE_DURATION("audio", "TapStage::ReadLock", "frame", frame, "length", frame_count);
  auto input_buffer = source_->ReadLock(dest_ref_time, frame, frame_count);
  if (!input_buffer) {
    return std::nullopt;
  }
  auto source_frac_frame_to_tap_frame = SourceFracFrameToTapFrame();

  uint8_t* input_ptr = reinterpret_cast<uint8_t*>(input_buffer->payload());
  int64_t output_buffer_frame =
      source_frac_frame_to_tap_frame.Apply(input_buffer->start().raw_value());
  uint32_t output_frames_outstanding = input_buffer->length().Floor();

  while (output_frames_outstanding > 0) {
    auto output_buffer =
        tap_->WriteLock(dest_ref_time, output_buffer_frame, output_frames_outstanding);
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

void TapStage::SetMinLeadTime(zx::duration min_lead_time) {
  ReadableStream::SetMinLeadTime(min_lead_time);
  source_->SetMinLeadTime(min_lead_time);
}

const TimelineFunction& TapStage::SourceFracFrameToTapFrame() {
  FX_DCHECK(source_->reference_clock() == tap_->reference_clock());

  auto source_snapshot = source_->ReferenceClockToFractionalFrames();
  auto tap_snapshot = tap_->ReferenceClockToFractionalFrames();
  if (source_snapshot.generation != source_generation_ ||
      tap_snapshot.generation != tap_generation_) {
    auto source_frac_frame_to_tap_frac_frame =
        tap_snapshot.timeline_function * source_snapshot.timeline_function.Inverse();
    auto frac_frame_to_frame = TimelineRate(1, FractionalFrames<uint32_t>(1).raw_value());
    source_frac_frame_to_tap_frame_ =
        TimelineFunction(frac_frame_to_frame) * source_frac_frame_to_tap_frac_frame;
    source_generation_ = source_snapshot.generation;
    tap_generation_ = tap_snapshot.generation;
  }
  return source_frac_frame_to_tap_frame_;
}

}  // namespace media::audio
