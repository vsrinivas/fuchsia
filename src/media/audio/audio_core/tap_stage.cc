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

std::optional<ReadableStream::Buffer> TapStage::ReadLock(int64_t dest_frame, size_t frame_count) {
  TRACE_DURATION("audio", "TapStage::ReadLock", "frame", dest_frame, "length", frame_count);

  // The source and destination share the same frame timelines, therefore we have no
  // need to translate these parameters.
  auto source_buffer = source_->ReadLock(dest_frame, frame_count);
  if (!source_buffer) {
    return std::nullopt;
  }

  // The source and tap may have different frame timelines.
  auto source_frac_frame_to_tap_frame = SourceFracFrameToTapFrame();

  uint8_t* source_ptr = reinterpret_cast<uint8_t*>(source_buffer->payload());
  int64_t tap_buffer_frame =
      source_frac_frame_to_tap_frame.Apply(source_buffer->start().raw_value());
  uint32_t tap_frames_outstanding = source_buffer->length().Floor();

  // Write the entire source_buffer to the tap stream.
  while (tap_frames_outstanding > 0) {
    auto tap_buffer = tap_->WriteLock(tap_buffer_frame, tap_frames_outstanding);
    if (!tap_buffer) {
      break;
    }

    int64_t tap_buffer_length = tap_buffer->length().Floor();
    FX_CHECK(tap_buffer_length <= std::numeric_limits<uint32_t>::max());
    uint32_t frames_copied =
        std::min(static_cast<uint32_t>(tap_buffer_length), tap_frames_outstanding);
    uint32_t bytes_copied = frames_copied * format().bytes_per_frame();
    memcpy(tap_buffer->payload(), source_ptr, bytes_copied);

    source_ptr += bytes_copied;
    tap_buffer_frame += frames_copied;
    tap_frames_outstanding -= frames_copied;
  }

  return source_buffer;
}

void TapStage::SetMinLeadTime(zx::duration min_lead_time) {
  ReadableStream::SetMinLeadTime(min_lead_time);
  source_->SetMinLeadTime(min_lead_time);
}

const TimelineFunction& TapStage::SourceFracFrameToTapFrame() {
  FX_DCHECK(source_->reference_clock() == tap_->reference_clock());

  auto source_snapshot = source_->ref_time_to_frac_presentation_frame();
  auto tap_snapshot = tap_->ref_time_to_frac_presentation_frame();
  if (source_snapshot.generation != source_generation_ ||
      tap_snapshot.generation != tap_generation_) {
    auto source_frac_frame_to_tap_frac_frame =
        tap_snapshot.timeline_function * source_snapshot.timeline_function.Inverse();
    auto frac_frame_to_frame = TimelineRate(1, Fixed(1).raw_value());
    source_frac_frame_to_tap_frame_ =
        TimelineFunction(frac_frame_to_frame) * source_frac_frame_to_tap_frac_frame;
    source_generation_ = source_snapshot.generation;
    tap_generation_ = tap_snapshot.generation;
  }
  return source_frac_frame_to_tap_frame_;
}

}  // namespace media::audio
