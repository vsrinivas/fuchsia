// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_BIN_USB_DEVICE_FRAME_WAITER_H_
#define SRC_CAMERA_BIN_USB_DEVICE_FRAME_WAITER_H_

#include <fuchsia/camera/cpp/fidl.h>
#include <lib/async/cpp/task.h>
#include <lib/async/cpp/wait.h>
#include <lib/async/default.h>
#include <lib/trace/event.h>
#include <zircon/status.h>
#include <zircon/types.h>

#include "src/lib/fsl/handles/object_info.h"

namespace camera {

// Wraps an async::Wait and frame release fence such that object lifetime requirements are enforced
// automatically, namely that the waited-upon object must outlive the wait object.
class FrameWaiter {
 public:
  FrameWaiter(async_dispatcher_t* dispatcher, std::vector<zx::eventpair> fences,
              fit::closure signaled)
      : fences_(std::move(fences)),
        pending_fences_(fences_.size()),
        signaled_(std::move(signaled)) {
    for (auto& fence : fences_) {
      waits_.push_back(std::make_unique<async::WaitOnce>(fence.get(), ZX_EVENTPAIR_PEER_CLOSED, 0));
      waits_.back()->Begin(dispatcher, fit::bind_member(this, &FrameWaiter::Handler));
    }
  }
  ~FrameWaiter() {
    for (auto& wait : waits_) {
      wait->Cancel();
    }
    signaled_ = nullptr;
    fences_.clear();
  }

 private:
  void Handler(async_dispatcher_t* dispatcher, async::WaitOnce* wait, zx_status_t status,
               const zx_packet_signal_t* signal) {
    if (status != ZX_OK) {
      return;
    }
    if (--pending_fences_ > 0) {
      return;
    }
    // |signaled_| may delete |this|, so move it to a local before calling it. This ensures captures
    // are persisted for the duration of the callback.
    auto signaled = std::move(signaled_);
    signaled();
  }
  std::vector<zx::eventpair> fences_;
  std::vector<std::unique_ptr<async::WaitOnce>> waits_;
  size_t pending_fences_;
  fit::closure signaled_;
};

}  // namespace camera

#endif  // SRC_CAMERA_BIN_USB_DEVICE_FRAME_WAITER_H_
