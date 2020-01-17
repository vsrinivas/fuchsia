// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/ring_buffer.h"

#include <trace/event.h>

#include "src/lib/syslog/cpp/logger.h"

namespace media::audio {

// static
std::shared_ptr<RingBuffer> RingBuffer::Create(zx::vmo vmo, uint32_t frame_size,
                                               uint32_t frame_count, bool input) {
  TRACE_DURATION("audio", "RingBuffer::Create");

  if (!vmo.is_valid()) {
    FX_LOGS(ERROR) << "Invalid VMO!";
    return nullptr;
  }

  if (!frame_size) {
    FX_LOGS(ERROR) << "Frame size may not be zero!";
    return nullptr;
  }

  uint64_t vmo_size;
  zx_status_t status = vmo.get_size(&vmo_size);

  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "Failed to get ring buffer VMO size";
    return nullptr;
  }

  uint64_t size = static_cast<uint64_t>(frame_size) * frame_count;
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

  return std::make_shared<RingBuffer>(std::move(vmo_mapper), frame_size, frame_count);
}

RingBuffer::RingBuffer(fzl::VmoMapper vmo_mapper, uint32_t frame_size, uint32_t frame_count)
    : vmo_mapper_(std::move(vmo_mapper)), frames_(frame_count), frame_size_(frame_size) {
  FX_CHECK(vmo_mapper_.start() != nullptr);
  FX_CHECK(vmo_mapper_.size() >= (frame_size * frame_count));
}

}  // namespace media::audio
