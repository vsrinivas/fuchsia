// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/services/mixer/mix/stream_sink_consumer_writer.h"

namespace media_audio {

StreamSinkConsumerWriter::Packet::Packet(std::shared_ptr<MemoryMappedBuffer> buffer,
                                         fuchsia_media2::wire::PayloadRange payload_range,
                                         void* payload)
    : buffer_(std::move(buffer)),
      payload_range_buffer_id_(payload_range.buffer_id),
      payload_range_offset_(payload_range.offset),
      payload_range_max_size_(payload_range.size) {}

void StreamSinkConsumerWriter::Packet::Recycle(std::shared_ptr<StreamConverter> stream_converter,
                                               std::optional<int64_t> timestamp) {
  write_offset_ = 0;
  stream_converter_ = std::move(stream_converter);
  timestamp_ = timestamp;
}

int64_t StreamSinkConsumerWriter::Packet::AppendData(int64_t frame_count, const void* data) {
  frame_count = std::min(frame_count, FramesRemaining());
  if (frame_count > 0) {
    // Since this data is going to an external consumer, it should be normalized (clipped).
    stream_converter_->CopyAndClip(data, buffer_->offset(payload_range_offset_ + write_offset_),
                                   frame_count);
    write_offset_ += frame_count * bytes_per_frame();
  }
  return frame_count;
}

int64_t StreamSinkConsumerWriter::Packet::AppendSilence(int64_t frame_count) {
  frame_count = std::min(frame_count, FramesRemaining());
  if (frame_count > 0) {
    stream_converter_->WriteSilence(buffer_->offset(payload_range_offset_ + write_offset_),
                                    frame_count);
    write_offset_ += frame_count * bytes_per_frame();
  }
  return frame_count;
}

int64_t StreamSinkConsumerWriter::Packet::FramesRemaining() const {
  return static_cast<int64_t>(payload_range_max_size_ - write_offset_) / bytes_per_frame();
}

fuchsia_audio::wire::Packet StreamSinkConsumerWriter::Packet::ToFidl(fidl::AnyArena& arena) const {
  FX_CHECK(0 <= write_offset_ < payload_range_max_size_)
      << "write_offset_=" << write_offset_  //
      << ", payload_range_max_size_=" << payload_range_max_size_;

  return fuchsia_audio::wire::Packet::Builder(arena)
      .payload({
          .buffer_id = payload_range_buffer_id_,
          .offset = payload_range_offset_,
          .size = write_offset_,
      })
      .timestamp(timestamp_ ? fuchsia_audio::wire::Timestamp::WithSpecified(arena, *timestamp_)
                            : fuchsia_audio::wire::Timestamp::WithUnspecifiedContinuous({}))
      .Build();
}

StreamSinkConsumerWriter::StreamSinkConsumerWriter(Args args)
    :  // TODO(fxbug.dev/87651): When ConsumerStage::Writers can write a different sample type than
       // the parent ConsumerStage, we'll have different source and dest formats here.
      stream_converter_(StreamConverter::Create(args.format, args.format)),
      media_ticks_per_frame_(
          TimelineRate::Product(args.media_ticks_per_ns, args.format.frames_per_ns().Inverse())),
      call_put_packet_(std::move(args.call_put_packet)),
      call_end_(std::move(args.call_end)),
      recycled_packet_queue_(std::move(args.recycled_packet_queue)) {}

void StreamSinkConsumerWriter::WriteData(int64_t start_frame, int64_t length, const void* data) {
  FX_CHECK(data);
  WriteInternal(start_frame, length, data);
}

void StreamSinkConsumerWriter::WriteSilence(int64_t start_frame, int64_t length) {
  WriteInternal(start_frame, length, nullptr);
}

void StreamSinkConsumerWriter::End() {
  // Emit the current packet, if any.
  SendCurrentPacket();
  // Continuity resets on "end".
  next_continuous_frame_ = std::nullopt;
  call_end_();
}

void StreamSinkConsumerWriter::WriteInternal(int64_t start_frame, int64_t length,
                                             const void* data) {
  FX_CHECK(length >= 0);

  // On discontinuities, emit the current packet before writing to `start_frame`. For
  // discontinuities after End, there's no current packet and this is a no-op.
  //
  // Otherwise, the discontinuity must be caused by an underflow. When an underflow happens, if the
  // discontinuity is small enough, we could write silence to `current_packet_` up to `start_frame`,
  // then continue using `current_packet_`, however this is more complex, and in practice underflows
  // should be rare anyway.
  if (next_continuous_frame_ && *next_continuous_frame_ != start_frame) {
    SendCurrentPacket();
  }

  while (length > 0) {
    if (!current_packet_) {
      auto next_packet = recycled_packet_queue_->pop();
      if (!next_packet) {
        // TODO(fxbug.dev/114393): Report overflow.
        return;
      }

      // Since media timestamps might have lower resolution than frame numbers, it may be difficult
      // for the client to determine if two packets are truly continuous. To avoid that problem, we
      // use "continuous" timestamps in place of explicit timestamp values, when possible.
      std::optional<int64_t> timestamp;
      if (next_continuous_frame_ && *next_continuous_frame_ == start_frame) {
        timestamp = std::nullopt;  // continuous
      } else {
        timestamp = media_ticks_per_frame_.Scale(start_frame);
      }

      current_packet_ = std::move(*next_packet);
      current_packet_->Recycle(stream_converter_, timestamp);
      next_continuous_frame_ = start_frame;
    }

    // Write as much data as possible.
    FX_CHECK(next_continuous_frame_ == start_frame);
    auto frames_written = data ? current_packet_->AppendData(length, data)  //
                               : current_packet_->AppendSilence(length);

    length -= frames_written;
    start_frame += frames_written;
    next_continuous_frame_ = start_frame;
    if (data) {
      data = static_cast<const char*>(data) +
             frames_written * stream_converter_->dest_format().bytes_per_frame();
    }

    // Emit the packet if full.
    if (current_packet_->FramesRemaining() == 0) {
      SendCurrentPacket();
    }
  }
}

void StreamSinkConsumerWriter::SendCurrentPacket() {
  if (current_packet_) {
    // After this move, `current_packet_ == nullptr`.
    call_put_packet_(std::move(current_packet_));
  }
}

}  // namespace media_audio
