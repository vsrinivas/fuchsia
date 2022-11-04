// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/v1/tap_stage.h"

#include <lib/syslog/cpp/macros.h>
#include <lib/trace/event.h>

namespace media::audio {

TapStage::TapStage(std::shared_ptr<ReadableStream> source, std::shared_ptr<WritableStream> tap)
    : ReadableStream("TapStage", source->format()),
      source_(std::move(source)),
      tap_(std::move(tap)),
      output_producer_(OutputProducer::Select(tap_->format().stream_type())) {
  FX_CHECK(source_->format() == tap_->format());
  FX_CHECK(source_->reference_clock()->koid() == tap_->reference_clock()->koid());
}

std::optional<ReadableStream::Buffer> TapStage::ReadLockImpl(ReadLockContext& ctx, Fixed dest_frame,
                                                             int64_t frame_count) {
  // TapStage always produces data on integrally-aligned frames.
  dest_frame = Fixed(dest_frame.Floor());

  // The source and tap may have different frame timelines.
  auto source_frac_frame_to_tap_frac_frame = SourceFracFrameToTapFracFrame();

  // First frame to populate in the tap stream.
  //
  // If the tap and dest streams are not integrally aligned, then the tap stream samples
  // from the dest stream using SampleAndHold: if dest frame 99.0 translates to tap frame
  // 1.X, then dest frame 99.0 is sampled by tap frame 2.0. Hence we round up.
  int64_t next_tap_frame =
      Fixed::FromRaw(source_frac_frame_to_tap_frac_frame.Apply(dest_frame.raw_value())).Ceiling();

  // Source and dest share the same frame timelines.
  auto source_buffer = source_->ReadLock(ctx, dest_frame, frame_count);
  if (!source_buffer) {
    WriteSilenceToTap(next_tap_frame, frame_count);
    return std::nullopt;
  }

  // Dest position is always integral. If source position is fractional, the dest stream
  // samples from the source stream using SampleAndHold: source frame 1.X is sampled at
  // dest frame 2.0, so round up.
  const Fixed first_source_frame = Fixed(source_buffer->start().Ceiling());

  // If there is a gap between dest_frame and the first source frame, write silence to fill the gap.
  if (first_source_frame > dest_frame) {
    const int64_t silent_frames = Fixed(first_source_frame - dest_frame).Floor();
    WriteSilenceToTap(next_tap_frame, silent_frames);
    next_tap_frame += silent_frames;
    frame_count -= silent_frames;
  }

  CopySourceToTap(*source_buffer, next_tap_frame, frame_count);

  // Forward the source buffer using the integral start position.
  return ForwardBuffer(std::move(source_buffer), first_source_frame);
}

void TapStage::WriteSilenceToTap(int64_t next_tap_frame, int64_t frame_count) {
  while (frame_count > 0) {
    auto tap_buffer = tap_->WriteLock(next_tap_frame, frame_count);
    if (!tap_buffer) {
      break;
    }

    // Required by WriteLock API.
    FX_CHECK(tap_buffer->start() >= next_tap_frame &&
             tap_buffer->end() <= next_tap_frame + frame_count)
        << "WriteLock(" << next_tap_frame << ", " << frame_count << ") "
        << "returned out-of-range buffer: [" << tap_buffer->start() << ", " << tap_buffer->end()
        << ")";

    // Fill the entire tap buffer.
    output_producer_->FillWithSilence(tap_buffer->payload(), tap_buffer->length());

    const int64_t frames_to_advance = tap_buffer->end() - next_tap_frame;
    next_tap_frame += frames_to_advance;
    frame_count -= frames_to_advance;
  }
}

void TapStage::CopySourceToTap(const ReadableStream::Buffer& source_buffer, int64_t next_tap_frame,
                               int64_t frame_count) {
  auto source_payload = reinterpret_cast<uintptr_t>(source_buffer.payload());
  frame_count = std::min(frame_count, source_buffer.length());

  while (frame_count > 0) {
    auto tap_buffer = tap_->WriteLock(next_tap_frame, frame_count);
    if (!tap_buffer) {
      break;
    }

    // Required by WriteLock API.
    FX_CHECK(tap_buffer->start() >= next_tap_frame &&
             tap_buffer->end() <= next_tap_frame + frame_count)
        << "WriteLock(" << next_tap_frame << ", " << frame_count << ") "
        << "returned out-of-range buffer: [" << tap_buffer->start() << ", " << tap_buffer->end()
        << ")";

    // A gap is possible if there was an underflow.
    const int64_t gap_frames = tap_buffer->start() - next_tap_frame;
    source_payload += static_cast<uint64_t>(gap_frames) * format().bytes_per_frame();

    // Copy enough frames to fill the entire tap buffer.
    // Per the above FX_CHECK, this cannot overflow the source buffer.
    uint64_t bytes_to_copy =
        static_cast<uint64_t>(tap_buffer->length()) * format().bytes_per_frame();
    memmove(tap_buffer->payload(), reinterpret_cast<const void*>(source_payload), bytes_to_copy);

    source_payload += bytes_to_copy;
    next_tap_frame += gap_frames + tap_buffer->length();
    frame_count -= gap_frames + tap_buffer->length();
  }
}

void TapStage::SetPresentationDelay(zx::duration external_delay) {
  // The tap does not introduce extra delay.
  ReadableStream::SetPresentationDelay(external_delay);
  source_->SetPresentationDelay(external_delay);
}

const TimelineFunction& TapStage::SourceFracFrameToTapFracFrame() {
  FX_DCHECK(source_->reference_clock()->koid() == tap_->reference_clock()->koid());

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
