// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_CAPTURE_PACKET_QUEUE_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_CAPTURE_PACKET_QUEUE_H_

#include <lib/fit/result.h>
#include <lib/fzl/vmo-mapper.h>

#include <deque>
#include <memory>
#include <mutex>
#include <unordered_map>

#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>
#include <fbl/slab_allocator.h>

#include "src/lib/fxl/synchronization/thread_annotations.h"
#include "src/media/audio/lib/format/format.h"

namespace media::audio {

// This class is thread safe.
class CapturePacketQueue {
 public:
  using CaptureAtCallback = fuchsia::media::AudioCapturer::CaptureAtCallback;
  using StreamPacket = fuchsia::media::StreamPacket;

  struct Packet;
  using AllocatorTraits = ::fbl::SlabAllocatorTraits<fbl::RefPtr<Packet>>;
  using Allocator = fbl::SlabAllocator<AllocatorTraits>;

  struct Packet : public fbl::SlabAllocated<AllocatorTraits>, public fbl::RefCounted<Packet> {
    StreamPacket stream_packet;
    void* buffer_start;
    void* buffer_end;
    CaptureAtCallback callback;
  };

  // Create a packet queue where all available packets are preallocated. To use payload_buffer
  // as a ring buffer, ensure that packets are released in the same order they are popped.
  // It is illegal to call Push on the returned packet queue.
  static fit::result<std::unique_ptr<CapturePacketQueue>, std::string> CreatePreallocated(
      const fzl::VmoMapper& payload_buffer, const Format& format, size_t frames_per_packet);

  // Create a packet queue where all packets will be dynamically allocated by Push.
  // It is illegal to call Release on packets returned from this queue.
  static fit::result<std::unique_ptr<CapturePacketQueue>, std::string> CreateDynamicallyAllocated(
      const fzl::VmoMapper& payload_buffer, const Format& format);

  // Number of pending packets.
  bool empty() const { return size() == 0; }
  size_t size() const;

  // Access to the front of the pending packet queue.
  // Front returns the first packet without removing it, while Pop removes it.
  // The queue must not be empty.
  fbl::RefPtr<Packet> Front() const;
  fbl::RefPtr<Packet> Pop();

  // Pop all packets from the queue.
  // Equivalent to repeatedly calling Pop() until size() == 0.
  std::vector<fbl::RefPtr<Packet>> PopAll();

  // Push a packet onto the the end of the queue.
  // The queue must have been created with CreateDynamicallyAllocated.
  // Returns an error if the packet is malformed.
  fit::result<void, std::string> Push(size_t offset_frames, size_t num_frames,
                                      CaptureAtCallback callback);

  // Release a packet back onto the queue. The packet must have been previously
  // returned by Pop and the queue must have been created with CreatePreallocated.
  // Returns an error if stream_packet was not in flight.
  fit::result<void, std::string> Release(const StreamPacket& stream_packet);

  // TODO(43507): This is a temporary flag to ease the transition. This will be exposed as a command
  // line flag for audio_core. This has no effect in DynamicallyAllocated mode.
  //
  // When false (the default), packets are automatically released after each call to Push.
  // This gives equivalent behavior to the "current" code, i.e., before the bug fix.
  // Otherwise, packets must be explicitly released.
  //
  // Eventually this flag will be deleted and the behavior will be hardcoded to "true".
  static void SetMustReleasePackets(bool b) { must_release_packets_ = b; }

 private:
  enum class Mode { Preallocated, DynamicallyAllocated };
  static bool must_release_packets_;

 public:
  // This needs to be public for std::make_unique.
  CapturePacketQueue(Mode mode, const fzl::VmoMapper& payload_buffer, const Format& format);

 private:
  fbl::RefPtr<Packet> Alloc(size_t frame_offset, size_t num_frames);
  fbl::RefPtr<Packet> PopLocked() FXL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  const Mode mode_;
  const fzl::VmoMapper& payload_buffer_;
  const size_t payload_buffer_frames_;
  const Format format_;

  Allocator allocator_;
  mutable std::mutex mutex_;

  // List of packets ready to be used as capture buffers.
  std::deque<fbl::RefPtr<Packet>> pending_ FXL_GUARDED_BY(mutex_);

  // Mapping from payload_offset to packet, where each packet is currently in use.
  // These packets will be returned to the pending list by Release().
  // For mode_ == Preallocated only.
  std::unordered_map<uint64_t, fbl::RefPtr<Packet>> inflight_ FXL_GUARDED_BY(mutex_);
};

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_CAPTURE_PACKET_QUEUE_H_
