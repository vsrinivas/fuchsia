// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/ui/sketchy/buffer/shared_buffer_pool.h"

#include <cmath>

namespace {

constexpr uint32_t kBaseBufferPower = 10;

}  // namespace

namespace sketchy_service {

SharedBufferPool::SharedBufferPool(scenic_lib::Session* session,
                                   escher::Escher* escher)
    : session_(session),
      escher_(escher),
      factory_(std::make_unique<escher::BufferFactory>(escher)),
      weak_factory_(this) {}

SharedBufferPtr SharedBufferPool::GetBuffer(vk::DeviceSize capacity_req,
                                            zx::event release_fence) {
  SharedBufferPtr buffer;
  auto capacity = GetBufferKey(capacity_req);
  if (!free_buffers_[capacity].empty()) {
    buffer = std::move(free_buffers_[capacity].back());
    free_buffers_[capacity].pop_back();
  } else {
    buffer = SharedBuffer::New(session_, factory_.get(), capacity);
  }
  used_buffers_.insert(buffer);

  // Listen to the fence release event from scenic, when the first subsequent
  // frame takes effect in Scenic. Once it's released, change the buffer state,
  // and recycle it if it's also released by canvas.
  auto pair = fence_listeners_.insert(
      std::make_unique<escher::FenceListener>(std::move(release_fence)));
  auto fence_listener = pair.first;
  (*fence_listener)->WaitReadyAsync(
      [weak = weak_factory_.GetWeakPtr(), fence_listener, buffer] {
        if (weak) {
          weak->fence_listeners_.erase(fence_listener);
          buffer->released_by_scenic_ = true;
          if (buffer->released_by_canvas()) {
            weak->RecycleBuffer(buffer);
          }
        }
      });
  return buffer;
}

void SharedBufferPool::ReturnBuffer(SharedBufferPtr buffer) {
  if (used_buffers_.find(buffer) == used_buffers_.end()) {
    FXL_DLOG(WARNING) << "buffer(" << buffer.get()
                      << ") does not come from pool(" << this << ")";
    return;
  }
  buffer->released_by_canvas_ = true;
  if (buffer->released_by_scenic()) {
    RecycleBuffer(buffer);
  }
}

void SharedBufferPool::RecycleBuffer(SharedBufferPtr buffer) {
  FXL_DCHECK(
      buffer && buffer->released_by_scenic() && buffer->released_by_canvas());
  buffer->Reset();
  auto capacity = buffer->capacity();
  FXL_DCHECK(capacity == GetBufferKey(capacity));
  free_buffers_[capacity].push_back(std::move(buffer));
}

vk::DeviceSize SharedBufferPool::GetBufferKey(vk::DeviceSize capacity_req) {
  FXL_CHECK(capacity_req >= 1);  // log2(x) >= 0 if x >= 1.
  uint32_t power = static_cast<uint32_t>(std::ceil(std::log2(capacity_req)));
  return 1U << std::max(kBaseBufferPower, power);
}

}  // namespace sketchy_service
