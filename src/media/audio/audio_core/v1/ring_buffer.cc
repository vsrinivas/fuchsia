// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/v1/ring_buffer.h"

#include <lib/syslog/cpp/macros.h>
#include <lib/trace/event.h>

#include <memory>

#include "src/media/audio/lib/processing/gain.h"

using SafeReadWriteFrameFn = media::audio::BaseRingBuffer::SafeReadWriteFrameFn;

namespace media::audio {
namespace {

template <class RingBufferT>
using MakeBufferT = std::function<std::optional<typename RingBufferT::Buffer>(
    int64_t start, int64_t length, void* payload)>;

template <class RingBufferT>
std::optional<typename RingBufferT::Buffer> LockBuffer(
    RingBufferT* b, SafeReadWriteFrameFn* safe_read_frame, SafeReadWriteFrameFn* safe_write_frame,
    int64_t requested_frame_start, int64_t requested_frame_count, bool is_read_lock,
    MakeBufferT<RingBufferT> make_buffer) {
  auto [ref_time_to_frac_presentation_frame, _] = b->ref_time_to_frac_presentation_frame();
  if (!ref_time_to_frac_presentation_frame.invertible()) {
    return std::nullopt;
  }

  int64_t valid_frame_start;
  int64_t valid_frame_end;  // one past the end
  if (is_read_lock) {
    valid_frame_end = (*safe_read_frame)() + 1;
    valid_frame_start = valid_frame_end - b->frames();
  } else {
    valid_frame_start = (*safe_write_frame)();
    valid_frame_end = valid_frame_start + b->frames();
  }

  int64_t requested_frame_end = requested_frame_start + requested_frame_count;
  if (requested_frame_start >= valid_frame_end || requested_frame_end <= valid_frame_start) {
    return std::nullopt;
  }

  // 'absolute' means the frame number not adjusted for the ring size.
  int64_t absolute_frame_start = std::max(requested_frame_start, valid_frame_start);
  int64_t absolute_frame_end = std::min(requested_frame_end, valid_frame_end);

  // 'local' is the frame number modulo ring size.
  int64_t local_frame_start = absolute_frame_start % b->frames();
  if (local_frame_start < 0) {
    local_frame_start += b->frames();
  }
  int64_t local_frame_end = absolute_frame_end % b->frames();
  if (local_frame_end < 0) {
    local_frame_end += b->frames();
  }
  if (local_frame_end <= local_frame_start) {
    local_frame_end = b->frames();
  }

  void* payload = b->virt() + (local_frame_start * b->format().bytes_per_frame());
  int64_t payload_frame_count = local_frame_end - local_frame_start;

  return make_buffer(absolute_frame_start, payload_frame_count, payload);
}

fbl::RefPtr<RefCountedVmoMapper> MapVmo(const Format& format, zx::vmo vmo, int64_t frame_count,
                                        bool writable) {
  if (!vmo.is_valid()) {
    FX_LOGS(ERROR) << "Invalid VMO!";
    return nullptr;
  }

  if (!format.bytes_per_frame()) {
    FX_LOGS(ERROR) << "Frame size may not be zero!";
    return nullptr;
  }

  uint64_t vmo_size;
  zx_status_t status = vmo.get_size(&vmo_size);

  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "Failed to get ring buffer VMO size";
    return nullptr;
  }

  uint64_t size = static_cast<uint64_t>(format.bytes_per_frame()) * frame_count;
  if (size > vmo_size) {
    FX_LOGS(ERROR) << "Driver-reported ring buffer size (" << size << ") is greater than VMO size ("
                   << vmo_size << ")";
    return nullptr;
  }

  // Map the VMO into our address space.
  // TODO(fxbug.dev/35022): How do I specify the cache policy for this mapping?
  zx_vm_option_t flags = ZX_VM_PERM_READ | (writable ? ZX_VM_PERM_WRITE : 0);
  auto vmo_mapper = fbl::MakeRefCounted<RefCountedVmoMapper>();
  status = vmo_mapper->Map(vmo, 0u, size, flags);

  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "Failed to map ring buffer VMO";
    return nullptr;
  }

  return vmo_mapper;
}

}  // namespace

BaseRingBuffer::BaseRingBuffer(
    const Format& format,
    fbl::RefPtr<VersionedTimelineFunction> ref_time_to_frac_presentation_frame,
    std::shared_ptr<Clock> audio_clock, fbl::RefPtr<RefCountedVmoMapper> vmo_mapper,
    int64_t frame_count)
    : vmo_mapper_(std::move(vmo_mapper)),
      frame_count_(frame_count),
      ref_time_to_frac_presentation_frame_(std::move(ref_time_to_frac_presentation_frame)),
      audio_clock_(std::move(audio_clock)) {
  FX_CHECK(vmo_mapper_->start() != nullptr);
  FX_CHECK(vmo_mapper_->size() >= static_cast<uint64_t>(format.bytes_per_frame() * frame_count));
}

ReadableRingBuffer::ReadableRingBuffer(
    const Format& format,
    fbl::RefPtr<VersionedTimelineFunction> ref_time_to_frac_presentation_frame,
    std::shared_ptr<Clock> audio_clock, fbl::RefPtr<RefCountedVmoMapper> vmo_mapper,
    int64_t frame_count, SafeReadWriteFrameFn safe_read_frame)
    : ReadableStream("ReadableRingBuffer", format),
      BaseRingBuffer(format, ref_time_to_frac_presentation_frame, std::move(audio_clock),
                     vmo_mapper, frame_count),
      safe_read_frame_(std::move(safe_read_frame)) {}

WritableRingBuffer::WritableRingBuffer(
    const Format& format,
    fbl::RefPtr<VersionedTimelineFunction> ref_time_to_frac_presentation_frame,
    std::shared_ptr<Clock> audio_clock, fbl::RefPtr<RefCountedVmoMapper> vmo_mapper,
    int64_t frame_count, SafeReadWriteFrameFn safe_write_frame)
    : WritableStream("WritableRingBuffer", format),
      BaseRingBuffer(format, ref_time_to_frac_presentation_frame, std::move(audio_clock),
                     vmo_mapper, frame_count),
      safe_write_frame_(std::move(safe_write_frame)) {}

// static
BaseRingBuffer::Endpoints BaseRingBuffer::AllocateSoftwareBuffer(
    const Format& format,
    fbl::RefPtr<VersionedTimelineFunction> ref_time_to_frac_presentation_frame,
    std::shared_ptr<Clock> audio_clock, int64_t frame_count,
    SafeReadWriteFrameFn safe_write_frame) {
  TRACE_DURATION("audio", "RingBuffer::AllocateSoftwareBuffer");

  size_t vmo_size = frame_count * format.bytes_per_frame();
  zx::vmo vmo;
  zx_status_t status = zx::vmo::create(vmo_size, 0, &vmo);
  FX_CHECK(status == ZX_OK);
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "Failed to allocate ring buffer VMO with size " << vmo_size;
    FX_CHECK(false);
  }

  auto vmo_mapper = MapVmo(format, std::move(vmo), frame_count, true);
  FX_DCHECK(vmo_mapper);

  // This is a normal producer/consumer ring buffer:
  //
  //   ----+-+-+----
  //   ... |R|W| ...
  //   ----+-+-+----
  //
  // If the safe_write_frame is at W, then frame W-1 must have been written, therefore the
  // safe_read_frame R = W-1. When this is used as the loopback buffer in an output pipeline,
  // the relationship between R, W and the output presentation frame (PO) is as follows:
  //
  //         |<-- delay -->|
  //   ----+--+-----------+-+-+----
  //   ... |PO|           |R|W| ...
  //   ----+--+-----------+-+-+----
  //
  // Frame PO is the frame currently being played at the output speaker. The delay between
  // W and PO is the "presentation delay" of the output pipeline. When a capture pipeline
  // hooks up to this loopback buffer, the capture pipeline can read any frame at R or
  // ealier. Note that frames are readable *before* they are presented at the speaker.
  // Conceptually, what's actually happening is:
  //
  //         |<-- delay -->|
  //   ----+--+-----------+-+--+----
  //   ... |PO|           |R|W | ...
  //       |  |           | |PC|
  //   ----+--+-----------+-+--+----
  //
  // Where PC is the current presentation frame for the capture pipeline. There's no actual
  // input device; the frame is being "presented" at this software buffer at the moment it
  // is written.
  //
  // In practice, loopback capture pipelines want to use timestamps that match the PTS of
  // the output pipeline. That is, the loopback capture wants to use PO for its timestamps,
  // not PC. This puts us in an unusual scenario where the capture pipeline can read frames
  // before they are presented.
  //
  // This explains why R = W-1 and why we pass ref_time_to_frac_presentation_frame to
  // both sides of the ring buffer.

  auto w =
      std::make_shared<WritableRingBuffer>(format, ref_time_to_frac_presentation_frame, audio_clock,
                                           vmo_mapper, frame_count, std::move(safe_write_frame));

  auto safe_read_frame = [w]() { return w->safe_write_frame_() - 1; };
  auto r =
      std::make_shared<ReadableRingBuffer>(format, ref_time_to_frac_presentation_frame, audio_clock,
                                           vmo_mapper, frame_count, std::move(safe_read_frame));

  return Endpoints{
      .reader = r,
      .writer = w,
  };
}

// static
std::shared_ptr<ReadableRingBuffer> BaseRingBuffer::CreateReadableHardwareBuffer(
    const Format& format,
    fbl::RefPtr<VersionedTimelineFunction> ref_time_to_frac_presentation_frame,
    std::shared_ptr<Clock> audio_clock, zx::vmo vmo, int64_t frame_count,
    SafeReadWriteFrameFn safe_read_frame) {
  TRACE_DURATION("audio", "RingBuffer::CreateReadableHardwareBuffer");

  auto vmo_mapper = MapVmo(format, std::move(vmo), frame_count, false);
  FX_DCHECK(vmo_mapper);

  return std::make_shared<ReadableRingBuffer>(
      format, std::move(ref_time_to_frac_presentation_frame), std::move(audio_clock),
      std::move(vmo_mapper), frame_count, std::move(safe_read_frame));
}

// static
std::shared_ptr<WritableRingBuffer> BaseRingBuffer::CreateWritableHardwareBuffer(
    const Format& format,
    fbl::RefPtr<VersionedTimelineFunction> ref_time_to_frac_presentation_frame,
    std::shared_ptr<Clock> audio_clock, zx::vmo vmo, int64_t frame_count,
    SafeReadWriteFrameFn safe_write_frame) {
  TRACE_DURATION("audio", "RingBuffer::CreateWritableHardwareBuffer");

  auto vmo_mapper = MapVmo(format, std::move(vmo), frame_count, true);
  FX_DCHECK(vmo_mapper);

  return std::make_shared<WritableRingBuffer>(
      format, std::move(ref_time_to_frac_presentation_frame), std::move(audio_clock),
      std::move(vmo_mapper), frame_count, std::move(safe_write_frame));
}

std::optional<ReadableStream::Buffer> ReadableRingBuffer::ReadLockImpl(ReadLockContext& ctx,
                                                                       Fixed frame,
                                                                       int64_t frame_count) {
  return LockBuffer<ReadableRingBuffer>(
      this, &safe_read_frame_, nullptr, frame.Floor(), frame_count, true,
      [this](int64_t start, int64_t length, void* payload) {
        // RingBuffers are synchronized only by time, which means there may not be a synchronization
        // happens-before edge connecting the last writer with the current reader, which means we
        // must invalidate our cache to ensure we read the latest data.
        //
        // This is especially important when the RingBuffer represents a buffer shared with HW,
        // because the last write may have happened very recently, increasing the likelihood that
        // our local cache is out-of-date. This is less important when the buffer is used in SW only
        // because it is more likely that the last write happened long enough ago that our cache has
        // been flushed in the interim, however to be strictly correct, a flush is needed in all
        // cases.
        int64_t payload_bytes = length * format().bytes_per_frame();
        zx_cache_flush(payload, payload_bytes, ZX_CACHE_FLUSH_DATA | ZX_CACHE_FLUSH_INVALIDATE);

        // Don't use a cached buffer. We don't need caching since we don't generate any
        // data dynamically.
        //
        // Another reason to use MakeUncachedBuffer is so we can validate the requested range
        // on every call. To see why, suppose a caller did the following:
        //
        //   1. ReadLock(0, 100)
        //   2. consume just 10 frames
        //   3. sleep for a long time (long enough to wrap around the ring buffer)
        //   4. ReadLock(10, 100)
        //
        // If we return a cached buffer at step 1, then step 4 will return the portion of that
        // cached buffer representing frames [10,99], but this is incorrect: the ring buffer has
        // wrapped around. Those frames are no longer available (step 4 should return null).
        return MakeUncachedBuffer(Fixed(start), length, payload, StreamUsageMask(),
                                  media_audio::kUnityGainDb);
      });
}

std::shared_ptr<ReadableRingBuffer> ReadableRingBuffer::Dup() const {
  return std::make_shared<ReadableRingBuffer>(format(), ref_time_to_frac_presentation_frame_,
                                              audio_clock_, vmo_mapper_, frame_count_,
                                              /* copy, don't move */ safe_read_frame_);
}

std::optional<WritableStream::Buffer> WritableRingBuffer::WriteLock(int64_t frame,
                                                                    int64_t frame_count) {
  return LockBuffer<WritableRingBuffer>(
      this, nullptr, &safe_write_frame_, frame, frame_count, false,
      [this](int64_t start, int64_t length, void* payload) {
        return std::make_optional<WritableStream::Buffer>(
            start, length, payload,
            // RingBuffers are synchronized only by time, which means there may not be a
            // synchronization happens-before edge connecting this writer with the next
            // reader. When this buffer is unlocked, we must flush our cache to ensure we
            // have published the latest data.
            [payload, payload_bytes = length * format().bytes_per_frame()]() {
              zx_cache_flush(payload, payload_bytes, ZX_CACHE_FLUSH_DATA);
            });
      });
}

BaseStream::TimelineFunctionSnapshot BaseRingBuffer::ReferenceClockToFixedImpl() const {
  if (!ref_time_to_frac_presentation_frame_) {
    return {
        .timeline_function = TimelineFunction(),
        .generation = kInvalidGenerationId,
    };
  }
  auto [timeline_function, generation] = ref_time_to_frac_presentation_frame_->get();
  return {
      .timeline_function = timeline_function,
      .generation = generation,
  };
}

BaseStream::TimelineFunctionSnapshot ReadableRingBuffer::ref_time_to_frac_presentation_frame()
    const {
  return BaseRingBuffer::ReferenceClockToFixedImpl();
}

BaseStream::TimelineFunctionSnapshot WritableRingBuffer::ref_time_to_frac_presentation_frame()
    const {
  return BaseRingBuffer::ReferenceClockToFixedImpl();
}

}  // namespace media::audio
