// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <garnet/lib/media/camera/simple_camera_lib/fenced_buffer.h>

#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

#include <memory>
#include <utility>

#include <lib/async/default.h>
#include <lib/fdio/io.h>
#include <lib/fxl/command_line.h>
#include <lib/fxl/log_settings_command_line.h>
#include <lib/fxl/logging.h>
#include <zx/vmar.h>
#include <zx/vmo.h>

namespace simple_camera {

std::unique_ptr<FencedBuffer> FencedBuffer::Create(
    uint64_t buffer_size,
    const zx::vmo& main_buffer,
    uint64_t offset,
    uint32_t index) {
  std::unique_ptr<FencedBuffer> b(new FencedBuffer);
  zx_status_t status = b->DuplicateAndMapVmo(buffer_size, main_buffer, offset);
  if (status != ZX_OK) {
    return nullptr;
  }

  zx::event acquire_fence;
  status = zx::event::create(0, &acquire_fence);
  if (status != ZX_OK) {
    printf("Failed to create acquire_fence.\n");
    return nullptr;
  }

  zx::event release_fence;
  status = zx::event::create(0, &release_fence);
  if (status != ZX_OK) {
    printf("Failed to create release_fence.\n");
    return nullptr;
  }
  release_fence.signal(0, ZX_EVENT_SIGNALED);

  b->index_ = index;

  b->acquire_fence_ = std::move(acquire_fence);
  b->release_fence_ = std::move(release_fence);

  return b;
}

FencedBuffer::FencedBuffer() = default;

FencedBuffer::~FencedBuffer() = default;

void FencedBuffer::OnReleaseFenceSignalled(
    async_dispatcher_t* dispatcher,
    async::WaitBase* wait,
    zx_status_t status,
    const zx_packet_signal* signal) {
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "AsyncWaiter received an error ("
                   << zx_status_get_string(status) << ").  Exiting.";
    // TODO(garratt): Store the error state and allow it to be queried somehow.
    return;
  }
  Reset();
  if (release_fence_callback_) {
    release_fence_callback_(this);
  }

  status = wait->Begin(dispatcher);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "AsyncWaiter failed to wait ("
                   << zx_status_get_string(status) << ").  Exiting.";
    // TODO(garratt): Store the error state and allow it to be queried somehow.
  }
}

void FencedBuffer::SetReleaseFenceHandler(BufferCallback callback) {
  if (!callback) {
    FXL_LOG(ERROR) << "callback is nullptr";
    return;
  }
  release_fence_callback_ = fbl::move(callback);
  release_fence_waiter_.set_object(release_fence_.get());
  release_fence_waiter_.set_trigger(ZX_EVENT_SIGNALED);
  // Clear the release fence, so we don't just trigger ourselves
  release_fence_.signal(ZX_EVENT_SIGNALED, 0);
  auto status = release_fence_waiter_.Begin(async_get_default_dispatcher());
  FXL_DCHECK(status == ZX_OK);
}

void FencedBuffer::Reset() {
  acquire_fence_.signal(ZX_EVENT_SIGNALED, 0);
  release_fence_.signal(ZX_EVENT_SIGNALED, 0);
  state_ = BufferState::kAvailable;
}

void FencedBuffer::Signal() {
  acquire_fence_.signal(0, ZX_EVENT_SIGNALED);
  state_ = BufferState::kReadLocked;
}

}  // namespace simple_camera
