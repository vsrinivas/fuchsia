// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/tap_stage.h"

#include <lib/syslog/cpp/macros.h>
#include <lib/trace/event.h>

namespace media::audio {

TapStage::TapStage(std::shared_ptr<ReadableStream> source, std::shared_ptr<WritableStream> tap)
    : ReadableStream(source->format()),
      source_(std::move(source)),
      tap_(std::move(tap)),
      output_producer_(OutputProducer::Select(tap_->format().stream_type())) {
  FX_CHECK(source_->format() == tap_->format());
  FX_CHECK(source_->reference_clock() == tap_->reference_clock());
}

std::optional<ReadableStream::Buffer> TapStage::ReadLock(Fixed dest_frame, size_t frame_count) {
  TRACE_DURATION("audio", "TapStage::ReadLock", "frame", dest_frame.Floor(), "length", frame_count);

  // The source and tap may have different frame timelines.
  auto source_frac_frame_to_tap_frac_frame = SourceFracFrameToTapFracFrame();

  // The source and destinations, however, share the same frame timelines, therefore we have no need
  // to translate these parameters.
  auto source_buffer = source_->ReadLock(dest_frame, frame_count);

  // We need to write some silence to the tap if some part of the frame range is not available in
  // the source stream. If a single write buffer extends beyond the end of the silent region it
  // is returned so that we can copy some frames into the remaining portion of the buffer.
  std::optional<WritableStream::Buffer> write_buffer;
  const int64_t silent_frames =
      source_buffer ? (source_buffer->start().Floor() - dest_frame.Floor()) : frame_count;
  if (silent_frames > 0) {
    const int64_t first_tap_frame =
        Fixed::FromRaw(source_frac_frame_to_tap_frac_frame.Apply(dest_frame.raw_value())).Floor();
    write_buffer = WriteSilenceToTap(first_tap_frame, silent_frames);
  }

  // If we have a source buffer, we need to copy frames into the tap.
  if (source_buffer) {
    // This is the first frame we need to populate in the tap stream.
    const Fixed first_tap_frame = Fixed::FromRaw(
        source_frac_frame_to_tap_frac_frame.Apply(source_buffer->start().raw_value()));

    // If we don't have a write buffer left over from writing silence, acquire one now. If we can't
    // get a write buffer then we have nothing to do.
    if (!write_buffer) {
      write_buffer = tap_->WriteLock(first_tap_frame.Floor(), source_buffer->length().Floor());
    }
    if (write_buffer) {
      CopyFrames(std::move(write_buffer), *source_buffer, source_frac_frame_to_tap_frac_frame);
    }
  }

  return source_buffer;
}

std::optional<WritableStream::Buffer> TapStage::WriteSilenceToTap(int64_t frame,
                                                                  int64_t frame_count) {
  int64_t last_frame_exclusive = frame + frame_count;
  while (frame_count > 0) {
    auto tap_buffer = tap_->WriteLock(frame, frame_count);
    if (!tap_buffer) {
      break;
    }

    size_t silent_frames =
        std::min(tap_buffer->end().Floor(), last_frame_exclusive) - tap_buffer->start().Floor();
    output_producer_->FillWithSilence(tap_buffer->payload(), silent_frames);
    if (tap_buffer->end() > last_frame_exclusive) {
      return tap_buffer;
    }

    frame = tap_buffer->end().Floor();
    frame_count = last_frame_exclusive - frame;
  }
  return std::nullopt;
}

void TapStage::CopyFrames(std::optional<WritableStream::Buffer> tap_buffer,
                          const ReadableStream::Buffer& source,
                          const TimelineFunction& source_frac_frame_to_tap_frac_frame) {
  Fixed first_available_frame =
      Fixed::FromRaw(source_frac_frame_to_tap_frac_frame.Apply(source.start().raw_value()));
  Fixed last_available_frame =
      Fixed::FromRaw(source_frac_frame_to_tap_frac_frame.Apply(source.end().raw_value()));

  while (tap_buffer) {
    // Compute the overlap between the read/write buffers.
    Fixed first_tap_frame_to_copy = std::max(tap_buffer->start(), first_available_frame);
    Fixed last_tap_frame_to_copy = tap_buffer->end();
    FX_CHECK(last_tap_frame_to_copy > first_tap_frame_to_copy);
    Fixed frames_to_copy = last_tap_frame_to_copy - first_tap_frame_to_copy;
    size_t bytes_to_copy = frames_to_copy.Floor() * format().bytes_per_frame();

    // We might have an offset into the source buffer.
    ssize_t source_buffer_offset =
        Fixed(first_tap_frame_to_copy - first_available_frame).Floor() * format().bytes_per_frame();
    void* source_ptr = reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(source.payload()) +
                                               source_buffer_offset);

    // We may also have an offset into the tap buffer if some part of this buffer was populated
    // with silence above.
    size_t tap_buffer_offset =
        Fixed(first_tap_frame_to_copy - tap_buffer->start()).Floor() * format().bytes_per_frame();
    void* tap_ptr = reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(tap_buffer->payload()) +
                                            tap_buffer_offset);

    memmove(tap_ptr, source_ptr, bytes_to_copy);

    // Get another buffer if needed.
    int64_t frames_remaining = Fixed(last_available_frame - tap_buffer->end()).Floor();
    if (frames_remaining <= 0) {
      break;
    }
    tap_buffer = tap_->WriteLock(tap_buffer->end().Floor(), frames_remaining);
  }
}

void TapStage::SetPresentationDelay(zx::duration external_delay) {
  // The tap does not introduce extra delay.
  ReadableStream::SetPresentationDelay(external_delay);
  source_->SetPresentationDelay(external_delay);
}

const TimelineFunction& TapStage::SourceFracFrameToTapFracFrame() {
  FX_DCHECK(source_->reference_clock() == tap_->reference_clock());

  auto source_snapshot = source_->ref_time_to_frac_presentation_frame();
  auto tap_snapshot = tap_->ref_time_to_frac_presentation_frame();
  if (source_snapshot.generation != source_generation_ ||
      tap_snapshot.generation != tap_generation_) {
    source_frac_frame_to_tap_frac_frame_ =
        tap_snapshot.timeline_function * source_snapshot.timeline_function.Inverse();
    source_generation_ = source_snapshot.generation;
    tap_generation_ = tap_snapshot.generation;
  }

  return source_frac_frame_to_tap_frac_frame_;
}

}  // namespace media::audio
