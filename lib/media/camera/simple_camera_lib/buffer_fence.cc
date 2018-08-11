// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <garnet/lib/media/camera/simple_camera_lib/buffer_fence.h>

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

namespace simple_camera {

std::unique_ptr<BufferFence> BufferFence::Create(uint32_t index) {
  std::unique_ptr<BufferFence> b(new BufferFence);
  zx::event acquire_fence;
  zx_status_t status = zx::event::create(0, &acquire_fence);
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

BufferFence::BufferFence() = default;

BufferFence::~BufferFence() {
 release_fence_waiter_.Cancel();
}

void BufferFence::OnReleaseFenceSignalled(
    async_dispatcher_t* dispatcher,
    async::WaitBase* wait,
    zx_status_t status,
    const zx_packet_signal* signal) {
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "AsyncWaiter received an error ("
                   << zx_status_get_string(status) << ").  Exiting.";
    // TODO(CAM-7): Store the error state and allow it to be queried somehow.
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
    // TODO(CAM-7): Store the error state and allow it to be queried somehow.
  }
}

void BufferFence::SetReleaseFenceHandler(BufferCallback callback) {
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

void BufferFence::Reset() {
  acquire_fence_.signal(ZX_EVENT_SIGNALED, 0);
  release_fence_.signal(ZX_EVENT_SIGNALED, 0);
}

void BufferFence::Signal() {
  acquire_fence_.signal(0, ZX_EVENT_SIGNALED);
}

}  // namespace simple_camera
