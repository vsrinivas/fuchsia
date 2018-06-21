// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/media/audio_server/driver_ring_buffer.h"

#include <lib/zx/vmar.h>

#include "lib/fxl/logging.h"

namespace media {
namespace audio {

// static
fbl::RefPtr<DriverRingBuffer> DriverRingBuffer::Create(zx::vmo vmo,
                                                       uint32_t frame_size,
                                                       uint32_t frame_count,
                                                       bool input) {
  auto ret = fbl::AdoptRef(new DriverRingBuffer());

  if (ret->Init(fbl::move(vmo), frame_size, frame_count, input) != ZX_OK) {
    return nullptr;
  }

  return ret;
}

DriverRingBuffer::~DriverRingBuffer() {
  if (virt_ != nullptr) {
    zx_vmar_unmap(vmo_.get(), reinterpret_cast<uintptr_t>(virt_), size_);
  }
}

zx_status_t DriverRingBuffer::Init(zx::vmo vmo, uint32_t frame_size,
                                   uint32_t frame_count, bool input) {
  if (!vmo.is_valid()) {
    FXL_LOG(ERROR) << "Invalid VMO!";
    return ZX_ERR_INVALID_ARGS;
  }

  if (!frame_size) {
    FXL_LOG(ERROR) << "Frame size may not be zero!";
    return ZX_ERR_INVALID_ARGS;
  }

  frame_size_ = frame_size;
  frames_ = frame_count;
  size_ = static_cast<uint64_t>(frame_size_ * frames_);

  zx_status_t res;
  uint64_t vmo_size;
  vmo_ = fbl::move(vmo);
  res = vmo_.get_size(&vmo_size);

  if (res != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to get ring buffer VMO size (res " << res << ")";
    return res;
  }

  if (size_ > vmo_size) {
    FXL_LOG(ERROR) << "Driver reported ring buffer size (" << size_
                   << ") is greater than VMO size (" << vmo_size << ")";
    return res;
  }

  // Map the VMO into our address space.
  // TODO(johngro) : How do I specify the cache policy for this mapping?
  uint32_t flags = ZX_VM_FLAG_PERM_READ | (input ? 0 : ZX_VM_FLAG_PERM_WRITE);
  res = zx_vmar_map_old(zx_vmar_root_self(), 0u, vmo_.get(), 0u, size_, flags,
                        reinterpret_cast<uintptr_t*>(&virt_));
  if (res != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to map ring buffer VMO (res " << res << ")";
    return res;
  }

  return res;
}

}  // namespace audio
}  // namespace media
