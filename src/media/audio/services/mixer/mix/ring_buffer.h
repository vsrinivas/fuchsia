// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_SERVICES_MIXER_MIX_RING_BUFFER_H_
#define SRC_MEDIA_AUDIO_SERVICES_MIXER_MIX_RING_BUFFER_H_

#include <zircon/types.h>

#include <memory>
#include <optional>

#include "src/media/audio/lib/clock/unreadable_clock.h"
#include "src/media/audio/lib/format2/format.h"
#include "src/media/audio/services/mixer/common/memory_mapped_buffer.h"
#include "src/media/audio/services/mixer/mix/packet_view.h"

namespace media_audio {

// Represents a ring buffer of audio data.
class RingBuffer : public std::enable_shared_from_this<RingBuffer> {
 public:
  struct Args {
    // Format of this ring buffer.
    Format format;

    // Reference clock used by this ring buffer.
    UnreadableClock reference_clock;

    // The actual buffer, which stores `buffer->content_size() / format.bytes_per_frame()` frames
    // per ring.
    std::shared_ptr<MemoryMappedBuffer> buffer;

    // The number of frames allocated to the producer.
    int64_t producer_frames;

    // The number of frames allocated to the consumer.
    int64_t consumer_frames;
  };

  static std::shared_ptr<RingBuffer> Create(Args args);

  // Wraps a PacketView with a destructor that flushes the payload.
  class WritablePacketView : public PacketView {
   public:
    explicit WritablePacketView(PacketView p) : PacketView(p) {}
    ~WritablePacketView();
  };

  // Returns a packet representing the given range of frames. If the range wraps around the buffer,
  // only the first part of the range is returned. Handles cache invalidation.
  [[nodiscard]] PacketView Read(int64_t start_frame, int64_t frame_count);

  // Like Read, but returns a wrapper around a PacketView that handles cache flushing. The caller
  // should hold onto the returned object until they are done with the write.
  [[nodiscard]] WritablePacketView PrepareToWrite(int64_t start_frame, int64_t frame_count);

  // Returns the format of this buffer.
  [[nodiscard]] const Format& format() const { return format_; }

  // Returns the clock used by this buffer.
  [[nodiscard]] UnreadableClock reference_clock() const { return reference_clock_; }

 private:
  explicit RingBuffer(Args args);
  PacketView PacketForRange(int64_t start_frame, int64_t frame_count);

  const Format format_;
  const UnreadableClock reference_clock_;
  const std::shared_ptr<MemoryMappedBuffer> buffer_;
  const int64_t total_frames_;
  const int64_t producer_frames_;
  const int64_t consumer_frames_;
};

}  // namespace media_audio

#endif  // SRC_MEDIA_AUDIO_SERVICES_MIXER_MIX_RING_BUFFER_H_
