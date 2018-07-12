// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/escher/flib/fence_set_listener.h"

#include <lib/async/cpp/task.h>
#include <lib/async/default.h>
#include <lib/zx/time.h>

#include "lib/fxl/logging.h"

namespace escher {

FenceSetListener::FenceSetListener(::fidl::VectorPtr<zx::event> fence_listeners)
    : fences_(std::move(fence_listeners)) {}

void FenceSetListener::WaitReadyAsync(fxl::Closure ready_callback) {
  if (!ready_callback)
    return;

  // Make sure callback was not set before.
  FXL_DCHECK(!ready_callback_);

  if (ready()) {
    async::PostTask(async_get_default_dispatcher(), std::move(ready_callback));
    return;
  }

  FXL_DCHECK(waiters_.empty());
  waiters_.reserve(fences_->size());
  int waiter_index = 0;

  // Wait for |kFenceSignalled| on each fence.
  for (auto& fence : *fences_) {
    auto wait = std::make_unique<async::Wait>(fence.get(),     // handle
                                              kFenceSignalled  // trigger
    );
    wait->set_handler(std::bind(&FenceSetListener::OnFenceSignalled, this,
                                waiter_index, std::placeholders::_3,
                                std::placeholders::_4));
    zx_status_t status = wait->Begin(async_get_default_dispatcher());
    FXL_CHECK(status == ZX_OK);

    waiters_.push_back(std::move(wait));
    waiter_index++;
  }

  ready_callback_ = std::move(ready_callback);
}

void FenceSetListener::OnFenceSignalled(size_t waiter_index, zx_status_t status,
                                        const zx_packet_signal* signal) {
  if (status == ZX_OK) {
    zx_signals_t pending = signal->observed;
    FXL_DCHECK(pending & kFenceSignalled);
    FXL_DCHECK(ready_callback_);

    num_signalled_fences_++;

    FXL_DCHECK(waiters_[waiter_index]);
    waiters_[waiter_index].reset();

    if (ready()) {
      fxl::Closure callback = std::move(ready_callback_);
      waiters_.clear();

      callback();
    }
  } else {
    FXL_LOG(ERROR) << "FenceSetListener::OnFenceSignalled received an "
                      "error status code: "
                   << status;

    // TODO(MZ-173): Close the session if there is an error, or if the fence
    // is closed.
  }
}

}  // namespace escher
