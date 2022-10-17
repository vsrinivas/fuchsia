// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/services/mixer/mix/ring_buffer.h"

#include <lib/syslog/cpp/macros.h>
#include <lib/zx/vmo.h>
#include <zircon/compiler.h>

#include <atomic>
#include <memory>
#include <optional>

#include "src/media/audio/lib/format2/fixed.h"
#include "src/media/audio/services/common/logging.h"
#include "src/media/audio/services/mixer/mix/ring_buffer_consumer_writer.h"
#include "src/media/audio/services/mixer/mix/simple_ring_buffer_producer_stage.h"

namespace media_audio {

using WritablePacketView = RingBuffer::WritablePacketView;

WritablePacketView::~WritablePacketView() {
  zx_cache_flush(payload(), length() * format().bytes_per_frame(), ZX_CACHE_FLUSH_DATA);
}

RingBuffer::RingBuffer(const Format& format, UnreadableClock reference_clock,
                       std::shared_ptr<MemoryMappedBuffer> buffer)
    : format_(format), reference_clock_(std::move(reference_clock)), buffer_(std::move(buffer)) {
  // std::atomic accesses not necessary during the constructor.
  FX_CHECK(buffer_);
}

PacketView RingBuffer::Read(const int64_t start_frame, const int64_t frame_count) {
  auto buffer = std::atomic_load(&buffer_);
  auto packet = PacketForRange(*buffer, start_frame, frame_count);

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
  const auto nullptr_buffer = std::shared_ptr<MemoryMappedBuffer>(nullptr);
  auto buffer = std::atomic_load(&buffer_);

  // Switch to the pending buffer, if requested.
  if (auto pending = std::atomic_exchange(&pending_buffer_, nullptr_buffer); unlikely(pending)) {
    int64_t old_total_frames = TotalFramesForBuffer(*buffer);
    int64_t new_total_frames = TotalFramesForBuffer(*pending);

    // Copy the old buffer into the pending buffer.
    int64_t start = start_frame - std::min(old_total_frames, new_total_frames);
    int64_t end = start_frame;

    while (end > start) {
      auto old_packet = PacketForRange(*buffer, start, end - start);
      auto new_packet = PacketForRange(*pending, start, end - start);

      const int64_t frames = std::min(old_packet.length(), new_packet.length());
      std::memmove(new_packet.payload(), old_packet.payload(), frames * format_.bytes_per_frame());
      start += frames;
    }

    // Swap in the new buffer. At this point, the new buffer is accessible to Read.
    std::atomic_store(&buffer_, pending);
    buffer = std::move(pending);
  }

  // Ring buffers are synchronized only by time, which means there may not be a synchronization
  // happens-before edge connecting the last writer with the current reader. When the write is
  // complete, we must flush our cache to ensure we have published the latest data.
  return WritablePacketView(PacketForRange(*buffer, start_frame, frame_count));
}

void RingBuffer::SetBufferAsync(std::shared_ptr<MemoryMappedBuffer> new_buffer) {
  std::atomic_store(&pending_buffer_, std::move(new_buffer));
}

int64_t RingBuffer::TotalFramesForBuffer(const MemoryMappedBuffer& buffer) const {
  return buffer.content_size() / format_.bytes_per_frame();
}

PacketView RingBuffer::PacketForRange(const MemoryMappedBuffer& buffer, const int64_t start_frame,
                                      const int64_t frame_count) {
  const int64_t end_frame = start_frame + frame_count;
  const int64_t total_frames = TotalFramesForBuffer(buffer);

  // Wrap the absolute frames around the ring to calculate the "relative" frames to be returned.
  int64_t relative_start_frame = start_frame % total_frames;
  if (relative_start_frame < 0) {
    relative_start_frame += total_frames;
  }
  int64_t relative_end_frame = end_frame % total_frames;
  if (relative_end_frame < 0) {
    relative_end_frame += total_frames;
  }
  if (relative_end_frame <= relative_start_frame) {
    relative_end_frame = total_frames;
  }

  return PacketView({
      .format = format_,
      .start = Fixed(start_frame),
      .length = relative_end_frame - relative_start_frame,
      .payload =
          buffer.offset(static_cast<size_t>(relative_start_frame * format_.bytes_per_frame())),
  });
}

}  // namespace media_audio
