// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_STREAM_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_STREAM_H_

#include <fuchsia/media/cpp/fidl.h>
#include <lib/fit/result.h>
#include <lib/media/cpp/timeline_function.h>
#include <lib/zx/time.h>

#include <optional>

#include <fbl/ref_ptr.h>

#include "src/media/audio/audio_core/format.h"
#include "src/media/audio/audio_core/packet.h"

namespace media::audio {

class BaseStream {
 public:
  BaseStream(Format format) : format_(format) {}
  virtual ~BaseStream() = default;

  // A snapshot of a |TimelineFunction| with an associated |generation|. If |generation| is equal
  // between two subsequent calls to |ReferenceClockToFractionalFrames|, then the
  // |timeline_function| is guaranteed to be unchanged.
  struct TimelineFunctionSnapshot {
    TimelineFunction timeline_function;
    uint32_t generation;
  };
  virtual TimelineFunctionSnapshot ReferenceClockToFractionalFrames() const = 0;

  const Format& format() const { return format_; }

  virtual void SetMinLeadTime(zx::duration min_lead_time) { min_lead_time_.store(min_lead_time); }
  zx::duration GetMinLeadTime() const { return min_lead_time_.load(); }

 private:
  Format format_;
  std::atomic<zx::duration> min_lead_time_{zx::duration(0)};
};

// A read-only stream of audio data.
class ReadableStream : public BaseStream {
 public:
  ReadableStream(Format format) : BaseStream(format) {}
  virtual ~ReadableStream() = default;

  class Buffer {
   public:
    Buffer(int64_t start, uint32_t length, void* payload, bool is_continuous)
        : Buffer(FractionalFrames<int64_t>(start), FractionalFrames<uint32_t>(length), payload,
                 is_continuous) {}
    Buffer(FractionalFrames<int64_t> start, FractionalFrames<uint32_t> length, void* payload,
           bool is_continuous)
        : payload_(payload), start_(start), length_(length), is_continuous_(is_continuous) {}

    FractionalFrames<int64_t> start() const { return start_; }
    FractionalFrames<int64_t> end() const { return start_ + length_; }
    FractionalFrames<uint32_t> length() const { return length_; }
    void* payload() const { return payload_; }

    // Indicates this packet is continuous with a packet previously returned from an immediately
    // preceding |ReadLock| call.
    //
    // Buffers may become discontinuous if, for example, and AudioRenderer is flushed and new
    // packets are provided; these new packets will not be assumed to be continuous with the
    // preceeding ones. Each |ReadableStream| implementation is reponsible for reporting any
    // discontinuity so that stream processors (ex: the mixer) may clear any intermediate state
    // based on the continuity of the stream.
    bool is_continuous() const { return is_continuous_; }

   private:
    void* payload_;
    FractionalFrames<int64_t> start_;
    FractionalFrames<uint32_t> length_;
    bool is_continuous_;
  };

  // ReadLock acquires a read lock on the stream and returns a buffer representing the requested
  // time range. Returns nullopt if no data is available for that time range.
  //
  // Each ReadLock call must be paired with a ReadUnlock call to unlock the stream, even when
  // ReadLock returns nullopt. Doing so ensures that sources which are attempting to flush the
  // pending queue are forced to wait if the front of the queue is involved in a mixing operation.
  // This, in turn, guarantees that audio packets are always returned to the user in the order which
  // they were queued in without forcing AudioRenderers to wait to queue new data if a mix operation
  // is in progress.
  //
  // When calling ReadUnlock, the caller should set release_buffer=true iff the buffer was fully
  // consumed.
  virtual std::optional<Buffer> ReadLock(zx::time now, int64_t frame, uint32_t frame_count) = 0;
  virtual void ReadUnlock(bool release_buffer) = 0;

  // Trims the stream by releasing any frames before |trim_threshold|.
  virtual void Trim(zx::time trim_threshold) = 0;

  // Hooks to add logging or metrics for [Partial] Underflow events.
  virtual void ReportUnderflow(FractionalFrames<int64_t> frac_source_start,
                               FractionalFrames<int64_t> frac_source_mix_point,
                               zx::duration underflow_duration) {}
  virtual void ReportPartialUnderflow(FractionalFrames<int64_t> frac_source_offset,
                                      int64_t dest_mix_offset) {}
};

// A write-only stream of audio data.
class WritableStream : public BaseStream {
 public:
  WritableStream(Format format) : BaseStream(format) {}
  virtual ~WritableStream() = default;

  class Buffer {
   public:
    Buffer(int64_t start, uint32_t length, void* payload)
        : Buffer(FractionalFrames<int64_t>(start), FractionalFrames<uint32_t>(length), payload) {}
    Buffer(FractionalFrames<int64_t> start, FractionalFrames<uint32_t> length, void* payload)
        : payload_(payload), start_(start), length_(length) {}

    FractionalFrames<int64_t> start() const { return start_; }
    FractionalFrames<int64_t> end() const { return start_ + length_; }
    FractionalFrames<uint32_t> length() const { return length_; }
    void* payload() const { return payload_; }

   private:
    void* payload_;
    FractionalFrames<int64_t> start_;
    FractionalFrames<uint32_t> length_;
  };

  // WriteLock acquires a write lock on the stream and returns a buffer representing the requested
  // time range. Returns nullopt if no data is available for that time range.
  //
  // Each WriteLock call must be paired with a WriteUnlock call to unlock the stream, even when
  // WriteLock returns nullopt.
  virtual std::optional<Buffer> WriteLock(zx::time now, int64_t frame, uint32_t frame_count) = 0;
  virtual void WriteUnlock() = 0;
};

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_STREAM_H_
