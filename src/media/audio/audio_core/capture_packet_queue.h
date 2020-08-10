// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_CAPTURE_PACKET_QUEUE_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_CAPTURE_PACKET_QUEUE_H_

#include <lib/fit/result.h>
#include <lib/fzl/vmo-mapper.h>
#include <lib/syslog/cpp/macros.h>

#include <condition_variable>
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

// This queue has two states:
//
// Initially, a packet is pushed onto a "pending" queue. The mixer will write to the first
// packet in the pending queue, and once the mixer is done, it will move that packet to the
// "ready" queue, meaning the packet is now ready to be sent to the client. The FIDL loop
// will pop packets from the ready queue as they become available.
//
// Packets are pushed onto the pending queue in one of two ways:
//
//   1. For preallocated queues, the pending queue is prepopulated automatically. After
//      a packet is popped from the ready queue, it can be added back to the pending queue
//      by Recycle().
//
//   2. For dynamically allocated queues, packets must be explicitly pushed onto the
//      pending queue, and once the packet is popped from the ready queue, the caller
//      takes permanent ownership.
//
// This class is thread safe.
class CapturePacketQueue {
 public:
  using CaptureAtCallback = fuchsia::media::AudioCapturer::CaptureAtCallback;
  using StreamPacket = fuchsia::media::StreamPacket;

  class Packet;
  using AllocatorTraits = ::fbl::SlabAllocatorTraits<fbl::RefPtr<Packet>>;
  using Allocator = fbl::SlabAllocator<AllocatorTraits>;

  // This class is thread safe.
  class Packet : public fbl::SlabAllocated<AllocatorTraits>, public fbl::RefCounted<Packet> {
   public:
    Packet(CaptureAtCallback cb, size_t nf, size_t pbo, char* pbs)
        : callback_(std::move(cb)),
          num_frames_(nf),
          payload_buffer_offset_(pbo),
          payload_buffer_start_(pbs) {}

    const CaptureAtCallback& callback() const { return callback_; }
    const StreamPacket& stream_packet() const {
      FX_CHECK(ready_.load());
      return stream_packet_;
    }
    zx::duration time_since_ready() const {
      FX_CHECK(ready_.load());
      return zx::clock::get_monotonic() - ready_time_;
    }

   private:
    friend class CapturePacketQueue;
    void Reset() FXL_EXCLUSIVE_LOCKS_REQUIRED(&CapturePacketQueue::mutex_) {
      state_ = State();
      ready_.store(false);
    }

    const CaptureAtCallback callback_;
    const size_t num_frames_;
    const size_t payload_buffer_offset_;
    char* const payload_buffer_start_;

    // This state is updated during mixing.
    struct State {
      int64_t capture_timestamp = fuchsia::media::NO_TIMESTAMP;
      uint32_t flags = 0;
      size_t filled_frames = 0;
    };
    State state_ FXL_GUARDED_BY(&CapturePacketQueue::mutex_);

    // These are set when the packet is moved from the pending queue to the ready queue.
    StreamPacket stream_packet_;
    zx::time ready_time_;
    std::atomic<bool> ready_;
  };

  // Create a packet queue where all available packets are preallocated. To use payload_buffer
  // as a ring buffer, ensure that packets are recycled in the same order they are popped.
  // It is illegal to call Push on the returned packet queue.
  static fit::result<std::shared_ptr<CapturePacketQueue>, std::string> CreatePreallocated(
      const fzl::VmoMapper& payload_buffer, const Format& format, size_t frames_per_packet);

  // Create a packet queue where all packets will be dynamically allocated by Push.
  // It is illegal to call Recycle on packets returned from this queue.
  static std::shared_ptr<CapturePacketQueue> CreateDynamicallyAllocated(
      const fzl::VmoMapper& payload_buffer, const Format& format);

  // Report whether the pending and ready queues are both empty.
  bool empty() const;

  // Report the number of pending and ready packets.
  size_t PendingSize() const;
  size_t ReadySize() const;

  // Start mixing the packet at the front of the mix queue.
  // Returns nullopt if the queue is empty.
  //
  // The returned PacketMixState object contains bookkeeping information about the mix.
  // The caller should update this state as necessary and pass that final updated state
  // to FinishMixerJob once this mix operation is ready. If the mix operation only partially
  // fills the packet, then the next call to NextMixerJob will return the same state that
  // was passed to FinishMixerJob (except with an updated mix_target).
  //
  // For example, a typical usage might look like:
  //
  //   while (true) {
  //     auto mix_state = pq->NextMixerJob();
  //     if (mix_state.capture_timestamp == NO_TIMESTAMP) {
  //       mix_state.capture_timestamp = current_timestamp;
  //     }
  //     if (mix_state.rames > max_mix_frames) {
  //       mix_state.frames = max_mix_frames;
  //     }
  //     mix(mix_state.target, state.mix_frames);
  //     pq->FinishMixerJob(state);
  //   }
  //
  struct PacketMixState {
    fbl::RefPtr<Packet> packet;
    int64_t capture_timestamp = fuchsia::media::NO_TIMESTAMP;
    uint32_t flags = 0;
    void* target = nullptr;
    size_t frames = 0;
  };
  std::optional<PacketMixState> NextMixerJob();

  // Complete the job started by the last call to NextMixerJob().
  enum class PacketMixStatus {
    // If the packet was fully mixed, it will be moved from the pending queue to
    // the back of the ready queue.
    Done,
    // If the packet was only partially mixed, we expect another call to NextMixerJob.
    // The packet will be left at the front of the pending queue.
    Partial,
    // If the packet was discarded by a concurrent call to DiscardPendingPackets,
    // the packet will be left alone.
    Discarded,
  };
  PacketMixStatus FinishMixerJob(const PacketMixState& state);

  // Atomically move all packets from the pending queue to the ready queue.
  void DiscardPendingPackets();

  // Pop a packet from the ready queue. Returns nullptr if the ready queue is empty.
  fbl::RefPtr<Packet> PopReady();

  // Push a packet onto the the end of the pending queue.
  // The queue must have been created with CreateDynamicallyAllocated.
  // Returns an error if the packet is malformed.
  fit::result<void, std::string> PushPending(size_t offset_frames, size_t num_frames,
                                             CaptureAtCallback callback);

  // Recycle a packet back onto the queue. The packet must have been previously
  // returned by Pop and the queue must have been created with CreatePreallocated.
  // Returns an error if stream_packet was not in flight.
  fit::result<void, std::string> Recycle(const StreamPacket& stream_packet);

  // Stop accepting packets. All further calls to PushPending and Recycle will be
  // ignored, and NextMixerJob will return nullopt.
  void Shutdown();

  // Block until the pending queue is non-empty or the queue has been shut down.
  void WaitForPendingPacket();

 private:
  enum class Mode { Preallocated, DynamicallyAllocated };

 public:
  // This needs to be public for std::make_unique.
  CapturePacketQueue(Mode mode, const fzl::VmoMapper& payload_buffer, const Format& format);

 private:
  fbl::RefPtr<Packet> Alloc(size_t frame_offset, size_t num_frames, CaptureAtCallback callback);
  void PopPendingLocked() FXL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  const Mode mode_;
  const fzl::VmoMapper& payload_buffer_;
  const size_t payload_buffer_frames_;
  const Format format_;

  Allocator allocator_;
  mutable std::mutex mutex_;

  // Set by Shutdown.
  bool shutdown_ FXL_GUARDED_BY(mutex_) = false;

  // Pending and ready queues.
  std::deque<fbl::RefPtr<Packet>> pending_ FXL_GUARDED_BY(mutex_);
  std::deque<fbl::RefPtr<Packet>> ready_ FXL_GUARDED_BY(mutex_);
  std::condition_variable pending_signal_ FXL_GUARDED_BY(mutex_);

  // Mapping from payload_offset to packet, for packets that have been popped from ready_.
  // These packets will be returned to pending_ by Recycle().
  // For mode_ == Preallocated only.
  std::unordered_map<uint64_t, fbl::RefPtr<Packet>> inflight_ FXL_GUARDED_BY(mutex_);
};

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_CAPTURE_PACKET_QUEUE_H_
