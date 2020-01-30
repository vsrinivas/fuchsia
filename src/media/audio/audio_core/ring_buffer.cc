// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/ring_buffer.h"

#include <trace/event.h>

#include "src/lib/syslog/cpp/logger.h"

namespace media::audio {

// static
std::shared_ptr<RingBuffer> RingBuffer::Allocate(
    const Format& format,
    fbl::RefPtr<VersionedTimelineFunction> reference_clock_to_fractional_frame,
    uint32_t frame_count, bool input) {
  TRACE_DURATION("audio", "RingBuffer::Allocate");
  size_t vmo_size = frame_count * format.bytes_per_frame();
  zx::vmo vmo;
  zx_status_t status = zx::vmo::create(vmo_size, 0, &vmo);
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "Failed to allocate ring buffer VMO with size " << vmo_size;
    return nullptr;
  }
  return RingBuffer::Create(format, std::move(reference_clock_to_fractional_frame), std::move(vmo),
                            frame_count, input);
}

std::shared_ptr<RingBuffer> RingBuffer::Create(
    const Format& format,
    fbl::RefPtr<VersionedTimelineFunction> reference_clock_to_fractional_frame, zx::vmo vmo,
    uint32_t frame_count, bool input) {
  TRACE_DURATION("audio", "RingBuffer::Create");

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
  zx_vm_option_t flags = ZX_VM_PERM_READ | (input ? 0 : ZX_VM_PERM_WRITE);
  fzl::VmoMapper vmo_mapper;
  status = vmo_mapper.Map(vmo, 0u, size, flags);

  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "Failed to map ring buffer VMO";
    return nullptr;
  }

  return std::make_shared<RingBuffer>(format, std::move(reference_clock_to_fractional_frame),
                                      std::move(vmo_mapper), frame_count);
}

RingBuffer::RingBuffer(const Format& format,
                       fbl::RefPtr<VersionedTimelineFunction> reference_clock_to_fractional_frame,
                       fzl::VmoMapper vmo_mapper, uint32_t frame_count)
    : Stream(format),
      vmo_mapper_(std::move(vmo_mapper)),
      frames_(frame_count),
      reference_clock_to_fractional_frame_(std::move(reference_clock_to_fractional_frame)) {
  FX_CHECK(vmo_mapper_.start() != nullptr);
  FX_CHECK(vmo_mapper_.size() >= (format.bytes_per_frame() * frame_count));
}

std::optional<Stream::Buffer> RingBuffer::LockBuffer(zx::time now, int64_t frame,
                                                     uint32_t frame_count) {
  auto [reference_clock_to_fractional_frame, _] = ReferenceClockToFractionalFrames();
  if (!reference_clock_to_fractional_frame.invertible()) {
    return std::nullopt;
  }

  // Compute the frame numbers currently valid in the ring buffer.
  int64_t last_valid_frame =
      FractionalFrames<int64_t>::FromRaw(reference_clock_to_fractional_frame.Apply(now.get()))
          .Floor();
  int64_t first_valid_frame = std::max<int64_t>(last_valid_frame - frames(), 0);
  if (frame >= last_valid_frame || (frame + frame_count) <= first_valid_frame) {
    return std::nullopt;
  }

  int64_t last_requested_frame = frame + frame_count;

  // 'absolute' here means the frame number not adjusted for the ring size. 'local' is the frame
  // number modulo ring size.
  int64_t first_absolute_frame = std::max(frame, first_valid_frame);
  int64_t first_frame_local = first_absolute_frame % frames();
  int64_t last_frame_local = std::min(last_requested_frame, last_valid_frame) % frames();
  if (last_frame_local < first_frame_local) {
    last_frame_local = frames();
  }
  return {Stream::Buffer(first_absolute_frame, last_frame_local - first_frame_local,
                         virt() + (first_frame_local * format().bytes_per_frame()), true)};
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
