// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/v1/stream.h"

#include <lib/syslog/cpp/macros.h>

#include <mutex>

#include "src/media/audio/audio_core/v1/logging_flags.h"
#include "src/media/audio/audio_core/v1/mixer/intersect.h"
#include "src/media/audio/lib/format/constants.h"

namespace {

// "ReadLock [start_frame, end_frame) stream_name (this-ptr)"
#define VERBOSE_READ_LOCK_PREFIX                                                              \
  ffl::String::DecRational << "ReadLock [" << dest_frame << ", "                              \
                           << Fixed(dest_frame + Fixed(frame_count)) << ") " << name() << "(" \
                           << static_cast<void*>(this) << ")"

// "Trim     [frame] stream_name (this-ptr)"
#define VERBOSE_TRIM_PREFIX                                                       \
  ffl::String::DecRational << "Trim     [" << dest_frame << "] " << name() << "(" \
                           << static_cast<void*>(this) << ")"

}  // namespace

namespace media::audio {

ReadableStream::ReadableStream(std::string n, Format format)
    : BaseStream(std::move(n), format),
      name_for_read_lock_(std::string(name()) + "::ReadLock"),
      name_for_trim_(std::string(name()) + "::Trim") {}

std::optional<ReadableStream::Buffer> ReadableStream::ReadLock(ReadLockContext& ctx,
                                                               Fixed dest_frame,
                                                               int64_t frame_count) {
  TRACE_DURATION("audio", name_for_read_lock_.c_str(), "dest_frame", dest_frame.Integral().Floor(),
                 "dest_frame.frac", dest_frame.Fraction().raw_value(), "frame_count", frame_count);

  // Nested locks are not allowed.
  FX_CHECK(!locked_) << VERBOSE_READ_LOCK_PREFIX << " already locked";

  if constexpr (kLogReadLocks) {
    VERBOSE_LOGS << VERBOSE_READ_LOCK_PREFIX;
  }
  DetectTimelineUpdate();

  // Once a frame has been consumed, it cannot be locked again.
  // We cannot travel backwards in time.
  if (next_dest_frame_) {
    FX_CHECK(dest_frame >= *next_dest_frame_)
        << VERBOSE_READ_LOCK_PREFIX
        << " travelled backwards in time; expected dest_frame >= " << *next_dest_frame_;
  }

  // Check if we can reuse a cached buffer.
  if (auto out = ReadFromCachedBuffer(dest_frame, frame_count); out) {
    if constexpr (kLogReadLocks) {
      VERBOSE_LOGS << VERBOSE_READ_LOCK_PREFIX << " --> (cached) [" << out->start() << ", "
                   << out->end() << "]";
    }
    return out;
  }

  cached_ = std::nullopt;
  auto buffer = ReadLockImpl(ctx, dest_frame, frame_count);
  if (!buffer) {
    if constexpr (kLogReadLocks) {
      VERBOSE_LOGS << VERBOSE_READ_LOCK_PREFIX << " --> null";
    }
    Trim(dest_frame + Fixed(frame_count));
    return std::nullopt;
  }

  // Empty buffers should use std::nullopt.
  FX_CHECK(buffer->length() > 0) << VERBOSE_READ_LOCK_PREFIX << " returned empty buffer ["
                                 << buffer->start() << ", " << buffer->end() << ")";

  // See constraints defined in stream.h.
  const Fixed buffer_lower_bound = dest_frame - Fixed(1);
  const Fixed buffer_max_end = dest_frame + Fixed(frame_count);

  if (buffer->cache_this_buffer_) {
    // See comments for MakeCachedBuffer.
    FX_CHECK(buffer->start() > buffer_lower_bound && buffer->start() < buffer_max_end)
        << VERBOSE_READ_LOCK_PREFIX << " returned out-of-range cached buffer [" << buffer->start()
        << ", " << buffer->end() << "), expected start > " << buffer_lower_bound << "&& start < "
        << buffer_max_end;
  } else {
    // See comments for MakeUncachedBuffer.
    FX_CHECK(buffer->start() > buffer_lower_bound && buffer->end() <= buffer_max_end)
        << VERBOSE_READ_LOCK_PREFIX << " returned out-of-range uncached buffer [" << buffer->start()
        << ", " << buffer->end() << "), expected start > " << buffer_lower_bound
        << "&& end <= " << buffer_max_end;

    FX_CHECK(buffer->length() <= frame_count)
        << VERBOSE_READ_LOCK_PREFIX << " returned too large uncached buffer [" << buffer->start()
        << ", " << buffer->end() << ")";
  }

  // Ready to lock this buffer.
  if constexpr (kLogReadLocks) {
    VERBOSE_LOGS << VERBOSE_READ_LOCK_PREFIX << " --> [" << buffer->start() << ", " << buffer->end()
                 << ")";
  }

  locked_ = true;
  if (!buffer->cache_this_buffer_) {
    return buffer;
  }

  cached_ = std::move(buffer);
  auto out = ReadFromCachedBuffer(dest_frame, frame_count);
  FX_CHECK(out) << VERBOSE_READ_LOCK_PREFIX << " bad cached buffer [" << cached_->start() << ", "
                << cached_->end() << ")";
  return out;
}

void ReadableStream::Trim(Fixed dest_frame) {
  TRACE_DURATION("audio", name_for_trim_.c_str(), "frame", dest_frame.Integral().Floor(),
                 "frame.frac", dest_frame.Fraction().raw_value());

  // Cannot be called while locked.
  FX_CHECK(!locked_) << VERBOSE_TRIM_PREFIX << " already locked";

  if constexpr (kLogTrims) {
    VERBOSE_LOGS << VERBOSE_TRIM_PREFIX;
  }
  DetectTimelineUpdate();

  // Advance the trim point.
  if (!next_dest_frame_) {
    next_dest_frame_ = dest_frame;
  } else if (dest_frame <= *next_dest_frame_) {
    return;  // already trimmed past dest_frame
  } else {
    next_dest_frame_ = dest_frame;
  }

  // Hold onto the cached buffer until it's entirely trimmed. Once the cached buffer
  // is trimmed, it's safe to discard the buffer and let TrimImpl discard any backing
  // state that was referenced by the buffer.
  if (cached_ && dest_frame < cached_->end()) {
    return;
  }

  cached_ = std::nullopt;
  TrimImpl(dest_frame);
}

std::optional<ReadableStream::Buffer> ReadableStream::ReadFromCachedBuffer(Fixed start_frame,
                                                                           int64_t frame_count) {
  if (!cached_) {
    return std::nullopt;
  }

  // Check if the requested range intersects the cached buffer.
  auto cached_packet = mixer::Packet{
      .start = cached_->start(),
      .length = cached_->length(),
      .payload = cached_->payload(),
  };
  auto isect = mixer::IntersectPacket(format(), cached_packet, start_frame, frame_count);
  if (!isect) {
    return std::nullopt;
  }

  // Since we might be locking a subset of cached_, we can't return cached_ directly,
  // Instead we return a proxy to cached_.
  return MakeUncachedBuffer(isect->start, isect->length, isect->payload, cached_->usage_mask(),
                            cached_->total_applied_gain_db());
}

std::optional<ReadableStream::Buffer> ReadableStream::MakeCachedBuffer(
    Fixed start_frame, int64_t frame_count, void* payload, StreamUsageMask usage_mask,
    float total_applied_gain_db) {
  // This buffer will be stored in cached_. It won't be returned to the ReadLock caller,
  // instead we'll use ReadFromCachedBuffer to return a proxy to this buffer.
  return ReadableStream::Buffer(start_frame, frame_count, payload, true /* use cache */, usage_mask,
                                total_applied_gain_db, [this](int64_t frames_consumed) mutable {
                                  // Trim is handled by the proxy (see ReadFromCachedBuffer).
                                  ReadUnlock();
                                });
}

std::optional<ReadableStream::Buffer> ReadableStream::MakeUncachedBuffer(
    Fixed start_frame, int64_t frame_count, void* payload, StreamUsageMask usage_mask,
    float total_applied_gain_db) {
  return ReadableStream::Buffer(
      start_frame, frame_count, payload, false /* don't use cache */, usage_mask,
      total_applied_gain_db,
      // Destructing this buffer unlocks the stream. Ensure the buffer
      // holds a reference to this stream until it's unlocked.
      [this, start_frame, stream = shared_from_this()](int64_t frames_consumed) {
        locked_ = false;
        Fixed trim_frame = start_frame + Fixed(frames_consumed);
        if (frames_consumed > 0) {
          previous_buffer_end_ = trim_frame;
        };
        Trim(trim_frame);
        ReadUnlock();
      });
}

std::optional<ReadableStream::Buffer> ReadableStream::ForwardBuffer(
    std::optional<Buffer>&& buffer, std::optional<Fixed> start_frame) {
  if (!buffer) {
    return std::nullopt;
  }

  // Logically, we are passing `buffer` to the closure below. However, if we pass `buffer` directly,
  // this creates a recursive type (ReadableStream::Buffer contains a dtor closure, which contains a
  // ReadableStream::Buffer), which means the compiler cannot prove that the closure can be created
  // without heap allocation. To break that circular type, we store the forwarded buffer in `this`.
  forwarded_buffer_ = std::move(buffer);

  auto buffer_start = start_frame ? *start_frame : buffer->start();
  return ReadableStream::Buffer(
      // Wrap the buffer with a proxy so we can be notified when the buffer is unlocked.
      buffer_start, forwarded_buffer_->length(), forwarded_buffer_->payload(),
      false /* don't cache */, forwarded_buffer_->usage_mask(),
      forwarded_buffer_->total_applied_gain_db(),
      // Destructing this proxy unlocks the stream. Ensure the proxy holds a reference
      // to this stream AND to forwarded_buffer_ until the proxy is unlocked.
      [this, buffer_start, stream = shared_from_this()](int64_t frames_consumed) mutable {
        locked_ = false;
        Fixed trim_frame = buffer_start + Fixed(frames_consumed);
        if (frames_consumed > 0) {
          previous_buffer_end_ = trim_frame;
        };
        // What is consumed from the proxy is also consumed from the forwarded buffer.
        forwarded_buffer_->set_frames_consumed(frames_consumed);
        // Destroy the forwarded buffer before calling Trim to ensure the source stream
        // is unlocked before it is Trim'd.
        forwarded_buffer_ = std::nullopt;
        Trim(trim_frame);
        ReadUnlock();
      });
}

void ReadableStream::DetectTimelineUpdate() {
  auto generation = ref_time_to_frac_presentation_frame().generation;
  if (timeline_function_generation_ && generation == *timeline_function_generation_) {
    return;
  }
  timeline_function_generation_ = generation;
  // The presentation timeline has changed, so reset the stream. Ideally we'd reset
  // the stream immediately after the timeline changes, however it's difficult to do
  // that with our existing concurrency model, hence this polling approach.
  next_dest_frame_ = std::nullopt;
  previous_buffer_end_ = std::nullopt;
  cached_ = std::nullopt;
}

}  // namespace media::audio
