// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_SERVICES_MIXER_MIX_RING_BUFFER_H_
#define SRC_MEDIA_AUDIO_SERVICES_MIXER_MIX_RING_BUFFER_H_

#include <zircon/types.h>

#include <atomic>
#include <memory>
#include <optional>

#include "src/media/audio/lib/clock/unreadable_clock.h"
#include "src/media/audio/lib/format2/format.h"
#include "src/media/audio/services/mixer/common/memory_mapped_buffer.h"
#include "src/media/audio/services/mixer/mix/packet_view.h"

namespace media_audio {

// Represents a single-producer multi-consumer ring buffer of audio data. A ring buffer represents a
// window into an infinite stream. Frame numbers are specified relative to this infinite stream --
// the translation into ring buffer offsets happens internally.
//
// ## Concurrency
//
// Code in this directory (`mix/`) is supposed to be single-threaded, but ring buffers are unusual.
// Ring buffers are concurrent by their very nature: the producer and consumer may be running in
// different threads, or even in different HW domains. Hence, all public methods in this class are
// safe to call from any thread, except `PrepareToWrite`, which must be called by the producer.
// The actual data in a ring buffer is synchronized by time, using one of two protocols, as
// described below.
//
// ### External producer or consumer
//
// If the ring buffer is shared with an external producer or consumer, such as a hardware device,
// then we divide the ring buffer into two adjacent regions: a "safe write" region, which the
// producer can write, and a "safe read" region, which the consumers can read. These regions meet at
// the current time: the "safe write" region is in the future while the "safe read" region is in the
// past. The producer and consumers must agree on these regions using some out-of-band mechanism.
//
// More information about this protocol can be found at the documentation for the
// `fuchsia.audio.RingBuffer` FIDL type.
//
// ### Splitters
//
// Each SplitterNode uses a RingBuffer to connect a source stream (which writes to the buffer) with
// one or more destination streams (which read from the buffer). SplitterNodes don't have a clean
// separation into "safe read" and "safe write" regions. In some cases, consumers are allowed to
// read from the same region the producer is writing to. See ../docs/splitters.h for a detailed
// description of this protocol.
class RingBuffer : public std::enable_shared_from_this<RingBuffer> {
 public:
  RingBuffer(const Format& format, UnreadableClock reference_clock,
             std::shared_ptr<MemoryMappedBuffer> buffer);

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
  // should hold onto the returned object until they are done with the write. Cannot be called
  // concurrently -- see class comments for more discussion.
  [[nodiscard]] WritablePacketView PrepareToWrite(int64_t start_frame, int64_t frame_count);

  // Changes the underlying buffer. This change happens asynchronously during the next call to
  // `PrepareToWrite`. If this is called multiple times before the next `PrepareToWrite`, only the
  // most recent call has any effect.
  void SetBufferAsync(std::shared_ptr<MemoryMappedBuffer> new_buffer);

  // Returns the format of this buffer.
  [[nodiscard]] const Format& format() const { return format_; }

  // Returns the clock used by this buffer.
  [[nodiscard]] UnreadableClock reference_clock() const { return reference_clock_; }

 private:
  int64_t TotalFramesForBuffer(const MemoryMappedBuffer& buffer) const;
  PacketView PacketForRange(const MemoryMappedBuffer& buffer, int64_t start_frame,
                            int64_t frame_count);

  const Format format_;
  const UnreadableClock reference_clock_;

  // TODO(fxbug.dev/111798): These must be accessed with atomic instructions (std::atomic_load and
  // std::atomic_store). These can be std::atomic<std::shared_ptr<>> after C++20 is available.
  std::shared_ptr<MemoryMappedBuffer> buffer_;          // current buffer
  std::shared_ptr<MemoryMappedBuffer> pending_buffer_;  // non-nullptr if a change is requested
};

}  // namespace media_audio

#endif  // SRC_MEDIA_AUDIO_SERVICES_MIXER_MIX_RING_BUFFER_H_
