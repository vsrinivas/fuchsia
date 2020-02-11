// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/ring_buffer.h"

#include <trace/event.h>

#include "src/lib/syslog/cpp/logger.h"

namespace media::audio {
namespace {

struct ReadableRingBufferTraits {
  static std::pair<int64_t, int64_t> ComputeValidFrameRange(int64_t position,
                                                            uint32_t ring_frames) {
    int64_t last_valid_frame = position;
    int64_t first_valid_frame = last_valid_frame - ring_frames;
    return {first_valid_frame, last_valid_frame};
  }
};

struct WritableRingBufferTraits {
  static std::pair<int64_t, int64_t> ComputeValidFrameRange(int64_t position,
                                                            uint32_t ring_frames) {
    int64_t first_valid_frame = position;
    int64_t last_valid_frame = first_valid_frame + ring_frames;
    return {first_valid_frame, last_valid_frame};
  }
};

struct CacheFlushMemoryTraits {
  static void CleanBufferOnLock(void* buffer, size_t length) {
    zx_cache_flush(buffer, length, ZX_CACHE_FLUSH_DATA | ZX_CACHE_FLUSH_INVALIDATE);
  }
};

struct DefaultMemoryTraits {
  static void CleanBufferOnLock(void* buffer, size_t length) {}
};

template <class RingBufferTraits, class MemoryTraits = DefaultMemoryTraits>
class RingBufferImpl : public RingBuffer {
 public:
  RingBufferImpl(const Format& format,
                 fbl::RefPtr<VersionedTimelineFunction> reference_clock_to_fractional_frames,
                 fbl::RefPtr<RefCountedVmoMapper> vmo_mapper, uint32_t frame_count,
                 Endpoint endpoint, uint32_t offset_frames)
      : RingBuffer(format, std::move(reference_clock_to_fractional_frames), std::move(vmo_mapper),
                   frame_count, endpoint, offset_frames) {}

  std::optional<Stream::Buffer> LockBuffer(zx::time now, int64_t frame,
                                           uint32_t frame_count) override {
    frame += offset_frames();
    auto [reference_clock_to_fractional_frame, _] = ReferenceClockToFractionalFrames();
    if (!reference_clock_to_fractional_frame.invertible()) {
      return std::nullopt;
    }

    int64_t ring_position_now =
        FractionalFrames<int64_t>::FromRaw(reference_clock_to_fractional_frame.Apply(now.get()))
            .Floor() +
        offset_frames();
    auto [first_valid_frame, last_valid_frame] =
        RingBufferTraits::ComputeValidFrameRange(ring_position_now, frames());
    if (frame >= last_valid_frame || (frame + frame_count) <= first_valid_frame) {
      return std::nullopt;
    }

    int64_t last_requested_frame = frame + frame_count;

    // 'absolute' here means the frame number not adjusted for the ring size. 'local' is the frame
    // number modulo ring size.
    int64_t first_absolute_frame = std::max(frame, first_valid_frame);

    int64_t first_frame_local = first_absolute_frame % frames();
    if (first_frame_local < 0) {
      first_frame_local += frames();
    }
    int64_t last_frame_local = std::min(last_requested_frame, last_valid_frame) % frames();
    if (last_frame_local <= first_frame_local) {
      last_frame_local = frames();
    }

    void* payload = virt() + (first_frame_local * format().bytes_per_frame());
    uint32_t payload_frames = last_frame_local - first_frame_local;
    size_t payload_bytes = payload_frames * format().bytes_per_frame();

    MemoryTraits::CleanBufferOnLock(payload, payload_bytes);

    return {Stream::Buffer(first_absolute_frame - offset_frames(), payload_frames, payload, true)};
  }
};

fbl::RefPtr<RefCountedVmoMapper> MapVmo(const Format& format, zx::vmo vmo, uint32_t frame_count,
                                        RingBuffer::VmoMapping vmo_mapping) {
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
  // TODO(35022): How do I specify the cache policy for this mapping?
  zx_vm_option_t flags =
      ZX_VM_PERM_READ | (vmo_mapping == RingBuffer::VmoMapping::kReadOnly ? 0 : ZX_VM_PERM_WRITE);
  auto vmo_mapper = fbl::MakeRefCounted<RefCountedVmoMapper>();
  status = vmo_mapper->Map(vmo, 0u, size, flags);

  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "Failed to map ring buffer VMO";
    return nullptr;
  }

  return vmo_mapper;
}

}  // namespace

// static
RingBuffer::Endpoints RingBuffer::AllocateSoftwareBuffer(
    const Format& format,
    fbl::RefPtr<VersionedTimelineFunction> reference_clock_to_fractional_frame,
    uint32_t frame_count, uint32_t frame_offset) {
  TRACE_DURATION("audio", "RingBuffer::AllocateSoftwareBuffer");

  size_t vmo_size = frame_count * format.bytes_per_frame();
  zx::vmo vmo;
  zx_status_t status = zx::vmo::create(vmo_size, 0, &vmo);
  FX_CHECK(status == ZX_OK);
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "Failed to allocate ring buffer VMO with size " << vmo_size;
    FX_CHECK(false);
  }

  auto vmo_mapper = MapVmo(format, std::move(vmo), frame_count, VmoMapping::kReadWrite);
  FX_DCHECK(vmo_mapper);

  return Endpoints{
      .reader = std::make_shared<RingBufferImpl<ReadableRingBufferTraits>>(
          format, reference_clock_to_fractional_frame, vmo_mapper, frame_count, Endpoint::kReadable,
          frame_offset),
      .writer = std::make_shared<RingBufferImpl<WritableRingBufferTraits>>(
          format, reference_clock_to_fractional_frame, vmo_mapper, frame_count, Endpoint::kWritable,
          frame_offset),
  };
}

std::shared_ptr<RingBuffer> RingBuffer::CreateHardwareBuffer(
    const Format& format,
    fbl::RefPtr<VersionedTimelineFunction> reference_clock_to_fractional_frame, zx::vmo vmo,
    uint32_t frame_count, VmoMapping vmo_mapping, Endpoint endpoint, uint32_t offset_frames) {
  TRACE_DURATION("audio", "RingBuffer::CreateHardwareBuffer");

  auto vmo_mapper = MapVmo(format, std::move(vmo), frame_count, vmo_mapping);
  FX_DCHECK(vmo_mapper);

  if (endpoint == Endpoint::kReadable) {
    return std::make_shared<RingBufferImpl<ReadableRingBufferTraits, CacheFlushMemoryTraits>>(
        format, std::move(reference_clock_to_fractional_frame), std::move(vmo_mapper), frame_count,
        endpoint, offset_frames);
  } else {
    return std::make_shared<RingBufferImpl<WritableRingBufferTraits>>(
        format, std::move(reference_clock_to_fractional_frame), std::move(vmo_mapper), frame_count,
        endpoint, offset_frames);
  }
}

RingBuffer::RingBuffer(const Format& format,
                       fbl::RefPtr<VersionedTimelineFunction> reference_clock_to_fractional_frame,
                       fbl::RefPtr<RefCountedVmoMapper> vmo_mapper, uint32_t frame_count,
                       Endpoint endpoint, uint32_t offset_frames)
    : Stream(format),
      endpoint_(endpoint),
      vmo_mapper_(std::move(vmo_mapper)),
      frames_(frame_count),
      reference_clock_to_fractional_frame_(std::move(reference_clock_to_fractional_frame)),
      offset_frames_(offset_frames) {
  FX_CHECK(vmo_mapper_->start() != nullptr);
  FX_CHECK(vmo_mapper_->size() >= (format.bytes_per_frame() * frame_count));
}

Stream::TimelineFunctionSnapshot RingBuffer::ReferenceClockToFractionalFrames() const {
  if (!reference_clock_to_fractional_frame_) {
    return {
        .timeline_function = TimelineFunction(),
        .generation = kInvalidGenerationId,
    };
  }
  auto [timeline_function, generation] = reference_clock_to_fractional_frame_->get();
  return {
      .timeline_function = timeline_function,
      .generation = generation,
  };
}

}  // namespace media::audio
