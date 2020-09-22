// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/escher/flib/fence_set_listener.h"

#include <lib/async/default.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/time.h>

namespace escher {

FenceSetListener::FenceSetListener(std::vector<zx::event> fence_listeners)
    : fences_(std::move(fence_listeners)) {}

void FenceSetListener::WaitReadyAsync(fit::closure ready_callback) {
  if (!ready_callback)
    return;

  // Make sure callback was not set before.
  FX_DCHECK(!ready_callback_);

  if (ready()) {
    // We store the task object so that, if we are deleted, the task is cancelled.
    task_ = std::make_unique<async::TaskClosure>([this, callback = std::move(ready_callback)]() {
      auto task = std::move(task_);
      callback();
    });
    task_->Post(async_get_default_dispatcher());

    return;
  }

  FX_DCHECK(waiters_.empty());
  waiters_.reserve(fences_.size());
  int waiter_index = 0;

  // Wait for |kFenceSignalled| on each fence.
  for (auto& fence : fences_) {
    auto wait = std::make_unique<async::Wait>(fence.get(),     // handle
                                              kFenceSignalled  // trigger
    );
    wait->set_handler(std::bind(&FenceSetListener::OnFenceSignalled, this, waiter_index,
                                std::placeholders::_3, std::placeholders::_4));
    zx_status_t status = wait->Begin(async_get_default_dispatcher());
    FX_CHECK(status == ZX_OK);

    waiters_.push_back(std::move(wait));
    waiter_index++;
  }

  ready_callback_ = std::move(ready_callback);
}

void FenceSetListener::OnFenceSignalled(size_t waiter_index, zx_status_t status,
                                        const zx_packet_signal* signal) {
  if (status == ZX_OK) {
    zx_signals_t pending = signal->observed;
    FX_DCHECK(pending & kFenceSignalled);
    FX_DCHECK(ready_callback_);

    num_signalled_fences_++;

    FX_DCHECK(waiters_[waiter_index]);
    waiters_[waiter_index].reset();

    if (ready()) {
      fit::closure callback = std::move(ready_callback_);
      waiters_.clear();

      callback();
    }
  } else {
    FX_LOGS(ERROR) << "FenceSetListener::OnFenceSignalled received an "
                      "error status code: "
                   << status;

    // TODO(fxbug.dev/23426): Close the session if there is an error, or if the fence
    // is closed.
  }
}

}  // namespace escher
