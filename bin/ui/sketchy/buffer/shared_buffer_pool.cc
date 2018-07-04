// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/ui/sketchy/buffer/shared_buffer_pool.h"

#include <cmath>

namespace {

constexpr uint32_t kBaseBufferPower = 10;

}  // namespace

namespace sketchy_service {

SharedBufferPool::SharedBufferPool(scenic::Session* session,
                                   escher::EscherWeakPtr weak_escher)
    : session_(session),
      escher_(weak_escher),
      factory_(std::make_unique<escher::BufferFactory>(std::move(weak_escher))),
      weak_factory_(this) {}

SharedBufferPtr SharedBufferPool::GetBuffer(vk::DeviceSize capacity_req) {
  SharedBufferPtr buffer;
  auto capacity = GetBufferKey(capacity_req);
  if (!free_buffers_[capacity].empty()) {
    buffer = std::move(free_buffers_[capacity].back());
    free_buffers_[capacity].pop_back();
  } else {
    buffer = SharedBuffer::New(session_, factory_.get(), capacity);
  }
  used_buffers_.insert(buffer);
  return buffer;
}

void SharedBufferPool::ReturnBuffer(SharedBufferPtr buffer,
                                    zx::event release_fence) {
  if (used_buffers_.find(buffer) == used_buffers_.end()) {
    FXL_DLOG(WARNING) << "buffer(" << buffer.get()
                      << ") does not come from pool(" << this << ")";
    return;
  }
  // Listen to the fence release event from scenic, when the first subsequent
  // frame takes effect in Scenic. Once it's released, recycle it. This should
  // be done in ReturnBuffer() instead of GetBuffer(), because otherwise, we
  // will miss the release signals for the frames that come after.
  auto pair = fence_listeners_.insert(
      std::make_unique<escher::FenceListener>(std::move(release_fence)));
  auto fence_listener = pair.first;
  (*fence_listener)->WaitReadyAsync([
    weak = weak_factory_.GetWeakPtr(), fence_listener, buffer
  ] {
    if (weak) {
      weak->fence_listeners_.erase(fence_listener);
      weak->RecycleBuffer(buffer);
    }
  });
}

void SharedBufferPool::RecycleBuffer(SharedBufferPtr buffer) {
  buffer->Reset();
  auto capacity = buffer->capacity();
  FXL_DCHECK(capacity == GetBufferKey(capacity));
  free_buffers_[capacity].push_back(std::move(buffer));
}

vk::DeviceSize SharedBufferPool::GetBufferKey(vk::DeviceSize capacity_req) {
  capacity_req = std::max(1UL, capacity_req);
  uint32_t power = static_cast<uint32_t>(std::ceil(std::log2(capacity_req)));
  return 1U << std::max(kBaseBufferPower, power);
}

}  // namespace sketchy_service
