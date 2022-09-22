// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/services/mixer/mix/ring_buffer.h"

#include <lib/syslog/cpp/macros.h>
#include <lib/zx/vmo.h>

#include <algorithm>
#include <optional>

#include "src/media/audio/lib/format2/fixed.h"
#include "src/media/audio/services/common/logging.h"
#include "src/media/audio/services/mixer/mix/ring_buffer_consumer_writer.h"
#include "src/media/audio/services/mixer/mix/simple_ring_buffer_producer_stage.h"

namespace media_audio {

using WritablePacketView = RingBuffer::WritablePacketView;

// static
std::shared_ptr<RingBuffer> RingBuffer::Create(Args args) {
  struct WithPublicCtor : public RingBuffer {
   public:
    WithPublicCtor(Args args) : RingBuffer(std::move(args)) {}
  };
  return std::make_shared<WithPublicCtor>(std::move(args));
}

RingBuffer::RingBuffer(Args args)
    : format_(args.format),
      reference_clock_(std::move(args.reference_clock)),
      buffer_(std::move(args.buffer)),
      total_frames_(buffer_->content_size() / format_.bytes_per_frame()),
      producer_frames_(args.producer_frames),
      consumer_frames_(args.consumer_frames) {
  FX_CHECK(buffer_);
  FX_CHECK(total_frames_ >= producer_frames_ + consumer_frames_)
      << "total_frames=" << total_frames_ << ", producer_frames=" << producer_frames_
      << ", consumer_frames=" << consumer_frames_;
  FX_CHECK(producer_frames_ > 0);
  FX_CHECK(consumer_frames_ > 0);
}

PacketView RingBuffer::Read(const int64_t start_frame, const int64_t frame_count) {
  FX_CHECK(frame_count <= producer_frames_) << "producer tried to access " << frame_count
                                            << " frames, more than limit of " << producer_frames_;

  auto packet = PacketForRange(start_frame, frame_count);

  // Ring buffers are synchronized only by time, which means there may not be a synchronization
  // happens-before edge connecting the last writer with the current reader, which means we must
  // invalidate our cache to ensure we read the latest data.
  //
  // This is especially important when the ring buffer represents a buffer shared with HW, because
  // the last write may have happened very recently, increasing the likelihood that our local cache
  // is out-of-date. This is less important when the buffer is used in SW only because it is more
  // likely that the last write happened long enough ago that our cache has been flushed in the
  // interim, however to be strictly correct, a flush is needed in all cases.
  const int64_t payload_size = packet.length() * format().bytes_per_frame();
  zx_cache_flush(packet.payload(), payload_size, ZX_CACHE_FLUSH_DATA | ZX_CACHE_FLUSH_INVALIDATE);

  return packet;
}

WritablePacketView RingBuffer::PrepareToWrite(const int64_t start_frame,
                                              const int64_t frame_count) {
  FX_CHECK(frame_count <= consumer_frames_) << "consumer tried to access " << frame_count
                                            << " frames, more than limit of " << consumer_frames_;

  // Ring buffers are synchronized only by time, which means there may not be a synchronization
  // happens-before edge connecting the last writer with the current reader. When the write is
  // complete, we must flush our cache to ensure we have published the latest data.
  return WritablePacketView(PacketForRange(start_frame, frame_count));
}

PacketView RingBuffer::PacketForRange(const int64_t start_frame, const int64_t frame_count) {
  const int64_t end_frame = start_frame + frame_count;

  // Wrap the absolute frames around the ring to calculate the "relative" frames to be returned.
  int64_t relative_start_frame = start_frame % total_frames_;
  if (relative_start_frame < 0) {
    relative_start_frame += total_frames_;
  }
  int64_t relative_end_frame = end_frame % total_frames_;
  if (relative_end_frame < 0) {
    relative_end_frame += total_frames_;
  }
  if (relative_end_frame <= relative_start_frame) {
    relative_end_frame = total_frames_;
  }

  return PacketView({
      .format = format_,
      .start = Fixed(start_frame),
      .length = relative_end_frame - relative_start_frame,
      .payload =
          buffer_->offset(static_cast<size_t>(relative_start_frame * format_.bytes_per_frame())),
  });
}

RingBuffer::WritablePacketView::~WritablePacketView() {
  zx_cache_flush(payload(), length() * format().bytes_per_frame(), ZX_CACHE_FLUSH_DATA);
}

}  // namespace media_audio
