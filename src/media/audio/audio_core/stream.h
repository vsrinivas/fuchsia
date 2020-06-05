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

#include "src/media/audio/audio_core/packet.h"
#include "src/media/audio/audio_core/stream_usage.h"
#include "src/media/audio/lib/format/format.h"

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
    using DestructorT = fit::callback<void(bool fully_consumed)>;

    Buffer(int64_t start, uint32_t length, void* payload, bool is_continuous,
           StreamUsageMask usage_mask, float gain_db, DestructorT dtor = nullptr)
        : dtor_(std::move(dtor)),
          payload_(payload),
          start_(start),
          length_(length),
          is_continuous_(is_continuous),
          usage_mask_(usage_mask),
          gain_db_(gain_db) {}

    Buffer(FractionalFrames<int64_t> start, FractionalFrames<uint32_t> length, void* payload,
           bool is_continuous, StreamUsageMask usage_mask, float gain_db,
           DestructorT dtor = nullptr)
        : dtor_(std::move(dtor)),
          payload_(payload),
          start_(start),
          length_(length),
          is_continuous_(is_continuous),
          usage_mask_(usage_mask),
          gain_db_(gain_db) {}

    ~Buffer() {
      if (dtor_) {
        dtor_(is_fully_consumed_);
      }
    }

    Buffer(Buffer&& rhs) = default;
    Buffer& operator=(Buffer&& rhs) = default;

    Buffer(const Buffer& rhs) = delete;
    Buffer& operator=(const Buffer& rhs) = delete;

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

    // Call this to indicate whether the buffer was fully consumed.
    // By default, we assume this is true.
    void set_is_fully_consumed(bool fully_consumed) { is_fully_consumed_ = fully_consumed; }

    StreamUsageMask usage_mask() const { return usage_mask_; }
    float gain_db() const { return gain_db_; }

   private:
    DestructorT dtor_;
    void* payload_;
    FractionalFrames<int64_t> start_;
    FractionalFrames<uint32_t> length_;
    bool is_continuous_;
    bool is_fully_consumed_ = true;
    StreamUsageMask usage_mask_;
    float gain_db_;
  };

  // ReadLock acquires a read lock on the stream and returns a buffer representing the requested
  // time range. Returns nullopt if no data is available for that time range. The buffer will remain
  // locked until it is destructed.
  //
  // For each stream, it is not legal to hold more than one lock at a time.
  //
  // TODO(50669): Some implementations (e.g., PacketQueue) disregard the requested time range and
  // can return data from any time range. Specify if this is allowed and fix implementations if not.
  virtual std::optional<Buffer> ReadLock(zx::time now, int64_t frame, uint32_t frame_count) = 0;

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
    using DestructorT = fit::callback<void()>;

    Buffer(int64_t start, uint32_t length, void* payload, DestructorT dtor = nullptr)
        : dtor_(std::move(dtor)), payload_(payload), start_(start), length_(length) {}

    ~Buffer() {
      if (dtor_) {
        dtor_();
      }
    }

    Buffer(Buffer&& rhs) = default;
    Buffer& operator=(Buffer&& rhs) = default;

    Buffer(const Buffer& rhs) = delete;
    Buffer& operator=(const Buffer& rhs) = delete;

    FractionalFrames<int64_t> start() const { return start_; }
    FractionalFrames<int64_t> end() const { return start_ + length_; }
    FractionalFrames<uint32_t> length() const { return length_; }
    void* payload() const { return payload_; }

   private:
    DestructorT dtor_;
    void* payload_;
    FractionalFrames<int64_t> start_;
    FractionalFrames<uint32_t> length_;
  };

  // WriteLock acquires a write lock on the stream and returns a buffer representing the requested
  // time range. Returns nullopt if no data is available for that time range. The buffer will remain
  // locked until it is destructed.
  //
  // For each stream, it is not legal to hold more than one lock at a time.
  virtual std::optional<Buffer> WriteLock(zx::time now, int64_t frame, uint32_t frame_count) = 0;
};

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_STREAM_H_
