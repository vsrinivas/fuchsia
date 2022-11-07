// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/v1/silence_padding_stream.h"

#include "src/media/audio/audio_core/shared/mixer/intersect.h"
#include "src/media/audio/audio_core/shared/mixer/output_producer.h"

namespace media::audio {

std::shared_ptr<ReadableStream> SilencePaddingStream::WrapIfNeeded(
    std::shared_ptr<ReadableStream> source, Fixed silence_frames, bool fractional_gaps_round_down) {
  return (silence_frames == Fixed(0)) ? source
                                      : Create(source, silence_frames, fractional_gaps_round_down);
}

std::shared_ptr<SilencePaddingStream> SilencePaddingStream::Create(
    std::shared_ptr<ReadableStream> source, Fixed silence_frames, bool fractional_gaps_round_down) {
  return std::make_shared<SilencePaddingStream>(source, silence_frames, fractional_gaps_round_down);
}

SilencePaddingStream::SilencePaddingStream(std::shared_ptr<ReadableStream> source,
                                           Fixed silence_frames, bool fractional_gaps_round_down)
    : ReadableStream("SilencePaddingStream." + std::string(source->name()), source->format()),
      // Round up because we need to generate an integer number of frames.
      silence_frames_(silence_frames.Ceiling()),
      fractional_gaps_round_down_(fractional_gaps_round_down),
      source_(source) {
  FX_CHECK(silence_frames > 0);
  silence_.resize(silence_frames_ * source->format().bytes_per_frame());
  auto op = OutputProducer::Select(source->format().stream_type());
  op->FillWithSilence(&silence_[0], silence_frames_);
}

std::optional<ReadableStream::Buffer> SilencePaddingStream::ReadLockImpl(ReadLockContext& ctx,
                                                                         Fixed dest_frame,
                                                                         int64_t frame_count) {
  // Read the next source buffer.
  std::optional<ReadableStream::Buffer> next_buffer;
  {
    Fixed source_start = dest_frame;
    Fixed dest_frame_end = dest_frame + Fixed(frame_count);
    // Advance to our source's next available frame. This is needed when the source stream
    // contains gaps. For example, given a sequence of calls:
    //
    //   ReadLock(ctx, 100, 10)
    //   ReadLock(ctx, 105, 10)
    //
    // If silence_frames_ = 5 and our source does not have any data for the range [100,110),
    // then at the first call, our source will return std::nullopt and we will return 5 frames
    // of silence. At the next call, the caller asks for frames 105, but the source has already
    // advanced to frame 110. We know that frames [105,110) are empty, so we must advance our
    // request to frames [110,115).
    if (auto next_available = source_->NextAvailableFrame(); next_available) {
      source_start = std::max(source_start, *next_available);
    }
    if (int64_t source_frames = Fixed(dest_frame_end - source_start).Floor(); source_frames > 0) {
      next_buffer = source_->ReadLock(ctx, source_start, source_frames);
    }
  }

  // We emit silent frames following each buffer:
  //
  // +--------------+                        +-------------+
  // | last_buffer_ | (silence_frames_) ...  | next_buffer |
  // +--------------+                        +-------------+
  //
  // If there are more than silence_frames_ separating last_buffer_ and next_buffer, we
  // leave those extra frames empty. We do not emit a silent buffer unless last_buffer_ and
  // next_buffer are separated by at least one full frame.
  if (last_buffer_) {
    Fixed silence_start = last_buffer_->end_frame;
    Fixed silence_end = silence_start + Fixed(silence_frames_);
    // Always generate an integral number of frames.
    const int64_t silence_length =
        (next_buffer && next_buffer->start() < silence_end)
            ? (fractional_gaps_round_down_ ? Fixed(next_buffer->start() - silence_start).Floor()
                                           : Fixed(next_buffer->start() - silence_start).Ceiling())
            : silence_frames_;
    // If the silent region intersects with our request, return a silent buffer.
    auto packet = mixer::Packet{
        .start = silence_start,
        .length = silence_length,
        .payload = &silence_[0],
    };
    auto isect = IntersectPacket(format(), packet, dest_frame, frame_count);
    if (isect) {
      // We are emitting silence before next_buffer: we have not consumed any frames.
      if (next_buffer) {
        next_buffer->set_frames_consumed(0);
        next_buffer = std::nullopt;
      }

      FX_CHECK(isect->length > 0);
      FX_CHECK(isect->length <= silence_frames_);
      return MakeCachedBuffer(isect->start, isect->length, &silence_[0], last_buffer_->usage_mask,
                              last_buffer_->total_applied_gain_db);
    }
  }

  // Passthrough next_buffer.
  if (!next_buffer) {
    return std::nullopt;
  }

  last_buffer_ = BufferInfo{
      .end_frame = next_buffer->end(),
      .usage_mask = next_buffer->usage_mask(),
      .total_applied_gain_db = next_buffer->total_applied_gain_db(),
  };
  return ForwardBuffer(std::move(next_buffer));
}

void SilencePaddingStream::TrimImpl(Fixed dest_frame) { source_->Trim(dest_frame); }

}  // namespace media::audio
